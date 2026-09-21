#include "app_ball_pd_monitor.h"

/* Small local square-root avoids a toolchain-specific math-library dependency. */
static float sqrt_nonnegative(float value)
{
    float estimate;
    uint8_t i;

    if (value <= 0.0f) return 0.0f;
    estimate = (value > 1.0f) ? value : 1.0f;
    for (i = 0U; i < 10U; ++i) {
        estimate = 0.5f * (estimate + value / estimate);
    }
    return estimate;
}

static float clampf(float value, float lower, float upper)
{
    if (value < lower) return lower;
    if (value > upper) return upper;
    return value;
}

static float s_kp = BALL_PD_KP_DEFAULT;
static float s_kd = BALL_PD_KD_DEFAULT;
static int16_t s_target_x = BALL_PD_PHYSICAL_BALANCE_X_PX;

void BallPdMonitor_Init(void)
{
    s_kp = BALL_PD_KP_DEFAULT;
    s_kd = BALL_PD_KD_DEFAULT;
    s_target_x = BALL_PD_PHYSICAL_BALANCE_X_PX;
}

void BallPdMonitor_SetTargetX(int16_t target_x)
{
    if (target_x < 0) target_x = 0;
    if (target_x > 799) target_x = 799;
    s_target_x = target_x;
}

int16_t BallPdMonitor_ApplyMotionCompensation(int16_t pd_requested_steps,
                                               const BallMotionState_t *motion,
                                               bool enabled)
{
    float compensation;
    float requested;

    if (!enabled || motion == 0) return pd_requested_steps;

    /* Forward motion makes the ball drift to the calibrated rear side. Positive
     * STEP pushes toward negative image x, so a rearward positive-x drift needs
     * negative compensation. yawRate<0 is a right turn; the calibrated turn
     * term follows the same sign convention. */
    compensation = -BALL_MOTION_FORWARD_REAR_X_SIGN *
                   (BALL_MOTION_COMP_SPEED_GAIN * motion->speed_avg_cm_s +
                    BALL_MOTION_COMP_ACCEL_GAIN * motion->accel_cm_s2) +
                   BALL_MOTION_COMP_DIFF_GAIN * motion->speed_diff_cm_s;
    if (motion->imu_valid) {
        compensation += BALL_MOTION_COMP_YAWRATE_GAIN * motion->yaw_rate_dps;
    }
    compensation = clampf(compensation, -BALL_MOTION_COMP_MAX_STEPS,
                          BALL_MOTION_COMP_MAX_STEPS);
    requested = (float)pd_requested_steps + compensation;
    return (int16_t)clampf(requested, (float)BALL_PD_REQUEST_STEP_MIN,
                           (float)BALL_PD_REQUEST_STEP_MAX);
}

void BallPdMonitor_GetGains(float *kp, float *kd)
{
    if (kp != 0) *kp = s_kp;
    if (kd != 0) *kd = s_kd;
}

bool BallPdMonitor_SetGains(float kp, float kd)
{
    if (!(kp >= BALL_PD_KP_MIN && kp <= BALL_PD_KP_MAX) ||
        !(kd >= BALL_PD_KD_MIN && kd <= BALL_PD_KD_MAX)) {
        return false;
    }
    s_kp = kp;
    s_kd = kd;
    return true;
}

BallPdMonitor_t BallPdMonitor_Update(const BallPosition_t *ball, uint32_t vision_age_ms)
{
    BallPdMonitor_t monitor = {0};
    float distance;
    float direction;
    float remaining;
    float desired_velocity;
    float planned_drive;
    float damping_drive;
    float damping_kd;
    float command_steps;
    float hold_bias_steps;
    float speed_magnitude;
    float center_hold_drive;
    float offcenter_hold_drive;

    monitor.target_x = s_target_x;
    monitor.vision_age_ms = vision_age_ms;
    if (ball == 0) {
        monitor.status = BALL_PD_NO_FRAME;
        return monitor;
    }

    monitor.x = ball->x;
    monitor.vx = ball->vx;
    monitor.confidence = ball->confidence;
    monitor.vx_for_d = (int16_t)clampf((float)ball->vx,
                                       -(float)BALL_PD_VELOCITY_LIMIT,
                                       (float)BALL_PD_VELOCITY_LIMIT);
    if (!ball->valid) {
        monitor.status = BALL_PD_INVALID_FRAME;
        return monitor;
    }
    if (ball->confidence < BALL_PD_MIN_CONFIDENCE) {
        monitor.status = BALL_PD_LOW_CONFIDENCE;
        return monitor;
    }
    if (vision_age_ms > BALL_PD_VISION_TIMEOUT_MS) {
        monitor.status = BALL_PD_TIMEOUT;
        return monitor;
    }

    monitor.status = BALL_PD_OK;
    monitor.error_px = (int16_t)clampf((float)(s_target_x - ball->x),
                                       -4096.0f, 4095.0f);

    /* Position only defines desired velocity. This is the sole moving-control
     * path: no prediction, capture state, hard brake table, or second position
     * loop can override it. */
    distance = (float)s_target_x - (float)ball->x;
    direction = (distance > 0.0f) ? 1.0f : ((distance < 0.0f) ? -1.0f : 0.0f);
    remaining = (distance >= 0.0f) ? distance : -distance;
    remaining -= (float)BALL_PD_RETURN_GUARD_PX;
    if (remaining < 0.0f) remaining = 0.0f;

    desired_velocity = sqrt_nonnegative(2.0f * BALL_PD_RETURN_DECEL_PX_S2 *
                                        remaining);
    desired_velocity = clampf(desired_velocity, 0.0f,
                              BALL_PD_RETURN_VMAX_PX_S);
    desired_velocity *= direction;

    /* Kp converts the planned return speed into drive. When the ball is moving
     * toward its target inside the approach zone, raise Kd continuously before
     * it reaches center. This starts slope withdrawal/braking earlier without
     * adding another position loop or a discontinuous target change. */
    planned_drive = s_kp * desired_velocity / BALL_PD_VELOCITY_TO_DRIVE_DIV;
    speed_magnitude = (float)monitor.vx_for_d;
    if (speed_magnitude < 0.0f) speed_magnitude = -speed_magnitude;
    damping_kd = s_kd;
    if ((remaining <= (float)BALL_PD_APPROACH_BRAKE_DISTANCE_PX) &&
        (speed_magnitude >= (float)BALL_PD_APPROACH_BRAKE_SPEED_PX_S) &&
        ((distance > 0.0f && monitor.vx_for_d > 0) ||
         (distance < 0.0f && monitor.vx_for_d < 0))) {
        damping_kd = BALL_PD_APPROACH_BRAKE_KD;
    }
    damping_drive = -damping_kd * (float)monitor.vx_for_d;
    damping_drive = clampf(damping_drive, -BALL_PD_DAMPING_STEP_LIMIT,
                           BALL_PD_DAMPING_STEP_LIMIT);
    /* At the center, the planned speed becomes small near the endpoint. Add a
     * bounded low-speed position correction so static friction cannot leave a
     * persistent offset (for example x≈296 while target is x=377). The sign is
     * the same as distance: a ball left of center needs more positive STEP. */
    center_hold_drive = 0.0f;
    speed_magnitude = (float)monitor.vx_for_d;
    if (speed_magnitude < 0.0f) speed_magnitude = -speed_magnitude;
    if ((s_target_x == BALL_PD_PHYSICAL_BALANCE_X_PX) &&
        (speed_magnitude <= (float)BALL_PD_CENTER_HOLD_SPEED_PX_S)) {
        center_hold_drive = BALL_PD_CENTER_HOLD_KP * distance;
        center_hold_drive = clampf(center_hold_drive,
                                   -BALL_PD_CENTER_HOLD_LIMIT,
                                   BALL_PD_CENTER_HOLD_LIMIT);

        /* Outside the ±1cm band, a low-speed ball is allowed to be more than
         * one pixel-corrected but still unable to move because of linkage
         * stiction. Guarantee the calibrated minimum inward correction only
         * there; inside the band the proportional term remains smooth. */
        if ((remaining > (float)BALL_PD_CENTER_ACCEPTANCE_PX) &&
            (center_hold_drive > 0.0f) &&
            (center_hold_drive < BALL_PD_CENTER_HOLD_MIN_STEPS)) {
            center_hold_drive = BALL_PD_CENTER_HOLD_MIN_STEPS;
        } else if ((remaining > (float)BALL_PD_CENTER_ACCEPTANCE_PX) &&
                   (center_hold_drive < 0.0f) &&
                   (center_hold_drive > -BALL_PD_CENTER_HOLD_MIN_STEPS)) {
            center_hold_drive = -BALL_PD_CENTER_HOLD_MIN_STEPS;
        }
    }

    offcenter_hold_drive = 0.0f;
    if (s_target_x != BALL_PD_PHYSICAL_BALANCE_X_PX) {
        offcenter_hold_drive = BALL_PD_OFFCENTER_HOLD_KP * distance;
        offcenter_hold_drive = clampf(offcenter_hold_drive,
                                      -BALL_PD_OFFCENTER_HOLD_LIMIT,
                                      BALL_PD_OFFCENTER_HOLD_LIMIT);
    }

    /* +7 STEP is the measured installation bias only at physical center.
     * At ±5cm, zero position and velocity error must request a level pipe
     * instead of retaining a positive/upward slope. */
    hold_bias_steps = (s_target_x == BALL_PD_PHYSICAL_BALANCE_X_PX)
                          ? BALL_PD_CENTER_HOLD_BIAS_STEPS
                          : BALL_PD_OFFCENTER_HOLD_BIAS_STEPS;
    command_steps = hold_bias_steps + planned_drive + damping_drive +
                    center_hold_drive + offcenter_hold_drive;

    /* The new long linkage has a static-friction zone at far endpoints. When
     * the ball is far from x=377 and nearly stationary, ensure a measured
     * inward tilt floor. As speed increases the normal velocity trajectory and
     * damping take over, preventing this floor from feeding a fast crossing. */
    if ((remaining >= (float)BALL_PD_REMOTE_START_DISTANCE_PX) &&
        (speed_magnitude <= (float)BALL_PD_REMOTE_START_SPEED_PX_S)) {
        if ((distance < 0.0f) && (command_steps > BALL_PD_REMOTE_RIGHT_STEPS)) {
            command_steps = BALL_PD_REMOTE_RIGHT_STEPS;
        } else if ((distance > 0.0f) &&
                   (command_steps < BALL_PD_REMOTE_LEFT_STEPS)) {
            command_steps = BALL_PD_REMOTE_LEFT_STEPS;
        }
    }

    monitor.p_term = planned_drive;
    monitor.d_term = damping_drive;
    /* 标定：正STEP使管道向负图像x方向推球。既有轨迹公式按反向机构建立，
     * 因此只围绕当前目标的静态偏置镜像动态量。中心使用+7 STEP，
     * ±5cm使用0 STEP；不能把中心偏置重新带入端点。 */
    command_steps = hold_bias_steps - (command_steps - hold_bias_steps);
    monitor.command = clampf(command_steps / BALL_PD_COMMAND_TO_STEP,
                             -BALL_PD_OUTPUT_LIMIT, BALL_PD_OUTPUT_LIMIT);
    monitor.requested_steps = (int16_t)clampf(command_steps,
        (float)BALL_PD_REQUEST_STEP_MIN,
        (float)BALL_PD_REQUEST_STEP_MAX);
    return monitor;
}

const char *BallPdMonitor_StatusText(BallPdStatus_t status)
{
    switch (status) {
    case BALL_PD_OK:             return "OK";
    case BALL_PD_NO_FRAME:       return "NO_FRAME";
    case BALL_PD_INVALID_FRAME:  return "INVALID";
    case BALL_PD_LOW_CONFIDENCE: return "LOW_CF";
    case BALL_PD_TIMEOUT:        return "TIMEOUT";
    default:                     return "UNKNOWN";
    }
}
