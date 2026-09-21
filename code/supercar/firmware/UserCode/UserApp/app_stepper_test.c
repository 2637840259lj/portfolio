#include "app_stepper_test.h"
#include "tmc2209_driver.h"
#include "sys_time.h"
#include <stdio.h>

static volatile StepperTestState_t s_state;
static int16_t s_target_steps;
static uint8_t s_good_vision_frames;
static uint32_t s_last_vision_seq;
static uint32_t s_auto_zero_ready_ms;
static bool s_center_locked;
static int16_t s_center_lock_target_steps;
static uint32_t s_center_lock_candidate_ms;
#if STEPPER_TRAVEL_CALIBRATION_MODE
static const int8_t s_calibration_direction = STEPPER_CALIBRATION_DIRECTION;
#endif

static int16_t clamp_target(int16_t value)
{
    if (value > STEPPER_CONTROL_TARGET_MAX) return STEPPER_CONTROL_TARGET_MAX;
    if (value < STEPPER_CONTROL_TARGET_MIN) return STEPPER_CONTROL_TARGET_MIN;
    return value;
}

static int32_t follower_hz_for_error(int32_t error_steps)
{
    int32_t magnitude = (error_steps < 0) ? -error_steps : error_steps;

    if (magnitude <= STEPPER_FINE_ERROR_STEPS) return STEPPER_FINE_STEP_HZ;
    if (magnitude <= STEPPER_SMALL_ERROR_STEPS) return STEPPER_SMALL_STEP_HZ;
    if (magnitude <= STEPPER_MEDIUM_ERROR_STEPS) return STEPPER_MEDIUM_STEP_HZ;
    return STEPPER_LARGE_STEP_HZ;
}

static void reset_tracking_bookkeeping(void)
{
    s_good_vision_frames = 0U;
    s_last_vision_seq = 0U;
    s_center_locked = false;
    s_center_lock_target_steps = 0;
    s_center_lock_candidate_ms = 0U;
}

static void stop_and_hold_visual_fault(const char *reason)
{
    tmc2209_stop_hold();
    reset_tracking_bookkeeping();
    s_state = STEPPER_TEST_VISUAL_FAULT_HOLD;
    printf("Stepper: VISUAL_FAULT_HOLD %s; STEP stopped, hold active; auto-resumes after %u fresh frames. K3 releases.\r\n",
           reason, (unsigned int)STEPPER_AUTO_REARM_GOOD_FRAMES);
}

static void start_visual_tracking(const char *origin)
{
    s_target_steps = (int16_t)tmc2209_get_position_steps();
    reset_tracking_bookkeeping();
    s_state = STEPPER_TEST_VISUAL_TRACKING;
    printf("Stepper: VISUAL_TRACKING %s; safe target range %d..+%d STEP, follower speed=%d/%d/%d/%d STEP/s. K3 releases immediately.\r\n",
           origin, STEPPER_CONTROL_TARGET_MIN, STEPPER_CONTROL_TARGET_MAX,
           STEPPER_FINE_STEP_HZ, STEPPER_SMALL_STEP_HZ,
           STEPPER_MEDIUM_STEP_HZ, STEPPER_LARGE_STEP_HZ);
}

void StepperTest_Init(void)
{
    tmc2209_init();
    s_state = STEPPER_TEST_SAFE_RELEASED;
    s_target_steps = 0;
    reset_tracking_bookkeeping();
#if STEPPER_TRAVEL_CALIBRATION_MODE
    s_auto_zero_ready_ms = 0U;
    printf("Stepper: TRAVEL_CALIBRATION ready. K1=zero, K2=move downward %d STEP at %d STEP/s, K3=release.\r\n",
           STEPPER_CALIBRATION_INCREMENT_STEPS, STEPPER_CALIBRATION_STEP_HZ);
#else
    s_auto_zero_ready_ms = g_system_ticks_ms + STEPPER_AUTO_ZERO_DELAY_MS;
    printf("Stepper: AUTO_ZERO pending; place tube at midpoint within %ums, then it holds/zeros automatically. K3 releases.\r\n",
           (unsigned int)STEPPER_AUTO_ZERO_DELAY_MS);
#endif
}

bool StepperTest_IsExclusive(void)
{
    /* Competition mode owns K1/K2/K3 for mission selection and start/stop.
     * Stepper safety remains available through vision fault handling and BLE
     * emergency stop, without stealing the competition control panel. */
    return false;
}

StepperTestState_t StepperTest_GetState(void)
{
    return s_state;
}

int16_t StepperTest_GetTargetSteps(void)
{
    return s_target_steps;
}

bool StepperTest_IsTuningSafe(void)
{
    int32_t position = tmc2209_get_position_steps();
    return (s_state == STEPPER_TEST_VISUAL_TRACKING) &&
           tmc2209_position_is_valid() &&
           position >= -STEPPER_FINE_ERROR_STEPS &&
           position <= STEPPER_FINE_ERROR_STEPS &&
           s_target_steps >= -STEPPER_FINE_ERROR_STEPS &&
           s_target_steps <= STEPPER_FINE_ERROR_STEPS;
}

void StepperTest_OnKey1(void)
{
    if (s_state == STEPPER_TEST_ESTOP_LOCKED) {
        s_state = STEPPER_TEST_SAFE_RELEASED;
    }
    if (s_state == STEPPER_TEST_VISUAL_TRACKING) return;

    tmc2209_enable_hold();
    tmc2209_set_position_zero();
    s_target_steps = 0;
    reset_tracking_bookkeeping();
    s_state = STEPPER_TEST_ZERO_READY;
#if STEPPER_TRAVEL_CALIBRATION_MODE
    printf("Stepper: CALIBRATION_ZERO pos=0; fixed downward direction=%+d. K2 moves %d STEP, K3 releases.\r\n",
           s_calibration_direction, STEPPER_CALIBRATION_INCREMENT_STEPS);
#else
    printf("Stepper: ZERO_READY pos=0; hold active. Auto-tracking waits for fresh vision.\r\n");
#endif
}

void StepperTest_OnKey2(void)
{
#if STEPPER_TRAVEL_CALIBRATION_MODE
    int32_t position;
    int32_t destination;
    int32_t command_step_hz;

    if (s_state != STEPPER_TEST_ZERO_READY) {
        printf("Stepper: K2 ignored; manually align midpoint and press K1 first.\r\n");
        return;
    }
    if (!tmc2209_position_is_valid()) {
        s_state = STEPPER_TEST_SAFE_RELEASED;
        printf("Stepper: K2 rejected; position invalid. Re-align midpoint then K1.\r\n");
        return;
    }

    position = tmc2209_get_position_steps();
    destination = position + (int32_t)s_calibration_direction *
                  STEPPER_CALIBRATION_INCREMENT_STEPS;
    s_target_steps = (int16_t)destination;
    command_step_hz = (s_calibration_direction > 0) ?
                      STEPPER_CALIBRATION_STEP_HZ : -STEPPER_CALIBRATION_STEP_HZ;
    if (!tmc2209_set_target_step_hz(command_step_hz)) {
        tmc2209_stop_hold();
        printf("Stepper: calibration move rejected.\r\n");
        return;
    }
    printf("Stepper: CALIBRATION_MOVE pos=%ld -> target=%ld, dir=%+d.\r\n",
           (long)position, (long)destination, s_calibration_direction);
#else
    if ((s_state != STEPPER_TEST_ZERO_READY) &&
        (s_state != STEPPER_TEST_VISUAL_FAULT_HOLD)) {
        printf("Stepper: K2 ignored; first manually align midpoint and press K1.\r\n");
        return;
    }
    if (!tmc2209_position_is_valid()) {
        s_state = STEPPER_TEST_SAFE_RELEASED;
        printf("Stepper: K2 rejected; position invalid. Re-align midpoint then K1.\r\n");
        return;
    }
    start_visual_tracking("manual re-arm");
#endif
}

void StepperTest_OnKey3Press(void)
{
    tmc2209_release_and_invalidate();
    s_target_steps = 0;
    reset_tracking_bookkeeping();
    s_state = STEPPER_TEST_ESTOP_LOCKED;
    printf("Stepper: ESTOP_LOCKED; ENN released and position invalid. Re-align midpoint then K1.\r\n");
}

void StepperTest_UpdateVisualRequest(int16_t requested_steps, int16_t error_px,
                                     int16_t vx_px_s, uint32_t vision_seq,
                                     bool stable_hold_target, bool visual_ok)
{
    int32_t position;
    int32_t error_steps;
    int32_t command_step_hz;
    int32_t requested_target;
    int32_t speed_magnitude;

#if STEPPER_TRAVEL_CALIBRATION_MODE
    (void)error_px;
    (void)vx_px_s;

    (void)requested_steps;
    (void)vision_seq;
    (void)stable_hold_target;
    (void)visual_ok;
    return;
#endif

    /* K3 emergency release remains explicit and prevents automatic restart.
     * SAFE_RELEASED is only the short power-on state before auto-zero. */
    if ((s_state == STEPPER_TEST_ESTOP_LOCKED) ||
        (s_state == STEPPER_TEST_SAFE_RELEASED)) {
        return;
    }
    if (!visual_ok) {
        if (s_state == STEPPER_TEST_VISUAL_TRACKING) {
            stop_and_hold_visual_fault("fresh frame lost/invalid");
        } else {
            reset_tracking_bookkeeping();
        }
        return;
    }

    if ((s_state == STEPPER_TEST_ZERO_READY) ||
        (s_state == STEPPER_TEST_VISUAL_FAULT_HOLD)) {
        if (vision_seq != s_last_vision_seq) {
            s_last_vision_seq = vision_seq;
            if (++s_good_vision_frames >= STEPPER_AUTO_REARM_GOOD_FRAMES) {
                /* 上电自动零位后无需等待“开始任务”按键：只要连续获得可靠
                 * 视觉帧就进入O点平衡。视觉故障恢复也沿用相同自动重启策略。 */
                start_visual_tracking("auto-ready");
            }
        }
        return;
    }
    if (s_state != STEPPER_TEST_VISUAL_TRACKING) return;
    if (!tmc2209_position_is_valid()) {
        stop_and_hold_visual_fault("position invalid");
        return;
    }

    /* The trajectory controller remains the sole source of the desired pipe
     * position. At high ball speed, however, limit a newly reversed target's
     * lead over the actual pipe position: a huge instantaneous target reversal
     * cannot be physically realized before the ball crosses center. */
    requested_target = clamp_target(requested_steps);
    position = tmc2209_get_position_steps();
    speed_magnitude = (int32_t)vx_px_s;
    if (speed_magnitude < 0) speed_magnitude = -speed_magnitude;
    if (speed_magnitude >= STEPPER_FAST_BALL_SPEED_PX_S) {
        if (requested_target > position + STEPPER_FAST_TARGET_LEAD_STEPS) {
            requested_target = position + STEPPER_FAST_TARGET_LEAD_STEPS;
        } else if (requested_target < position - STEPPER_FAST_TARGET_LEAD_STEPS) {
            requested_target = position - STEPPER_FAST_TARGET_LEAD_STEPS;
        }
    }

    /* A verified settle locks the real pipe slope, not an old requested
     * target. This applies both at O and at the final -5cm endpoint: when the
     * ball has passed the task verdict, keep the physical slope that actually
     * holds it instead of commanding the mechanism back to coordinate 0.
     * Any meaningful displacement or motion releases the lock before following
     * a new visual request. */
    if (s_center_locked) {
        if ((error_px > STEPPER_CENTER_UNLOCK_ERROR_PX) ||
            (error_px < -STEPPER_CENTER_UNLOCK_ERROR_PX) ||
            (speed_magnitude > STEPPER_CENTER_UNLOCK_SPEED_PX_S)) {
            s_center_locked = false;
            s_center_lock_candidate_ms = 0U;
        } else {
            requested_target = s_center_lock_target_steps;
        }
    }

    if (!s_center_locked) {
        if (stable_hold_target &&
            (error_px <= STEPPER_CENTER_LOCK_ERROR_PX) &&
            (error_px >= -STEPPER_CENTER_LOCK_ERROR_PX) &&
            (speed_magnitude <= STEPPER_CENTER_LOCK_SPEED_PX_S)) {
            if (s_center_lock_candidate_ms == 0U) {
                s_center_lock_candidate_ms = g_system_ticks_ms;
            } else if ((uint32_t)(g_system_ticks_ms - s_center_lock_candidate_ms) >=
                       STEPPER_CENTER_LOCK_HOLD_MS) {
                s_center_locked = true;
                s_center_lock_target_steps = clamp_target((int16_t)position);
                requested_target = s_center_lock_target_steps;
            }
        } else {
            s_center_lock_candidate_ms = 0U;
        }
    }

    s_target_steps = clamp_target((int16_t)requested_target);
    error_steps = (int32_t)s_target_steps - position;
    command_step_hz = follower_hz_for_error(error_steps);
    if (error_steps < 0) command_step_hz = -command_step_hz;
    else if (error_steps == 0) command_step_hz = 0;

    if (!tmc2209_set_target_step_hz(command_step_hz) && command_step_hz != 0) {
        stop_and_hold_visual_fault("motor command rejected");
    }
}

void StepperTest_Task(void)
{
#if STEPPER_TRAVEL_CALIBRATION_MODE
    if ((s_state == STEPPER_TEST_ZERO_READY) && tmc2209_position_is_valid()) {
        int32_t position = tmc2209_get_position_steps();
        int32_t remaining = (int32_t)s_target_steps - position;

        if (((s_calibration_direction > 0) && (remaining <= 0)) ||
            ((s_calibration_direction < 0) && (remaining >= 0))) {
            if (tmc2209_get_target_step_hz() != 0) {
                tmc2209_stop_hold();
                s_target_steps = (int16_t)position;
                printf("Stepper: CALIBRATION_REACHED pos=%ld; hold active.\r\n",
                       (long)position);
            }
        }
    }
    return;
#endif

    if ((s_state == STEPPER_TEST_SAFE_RELEASED) &&
        ((int32_t)(g_system_ticks_ms - s_auto_zero_ready_ms) >= 0)) {
        tmc2209_enable_hold();
        tmc2209_set_position_zero();
        s_target_steps = 0;
        reset_tracking_bookkeeping();
        s_state = STEPPER_TEST_ZERO_READY;
        printf("Stepper: AUTO_ZERO_READY pos=0; hold active; waiting for %u fresh vision frames.\r\n",
               (unsigned int)STEPPER_AUTO_REARM_GOOD_FRAMES);
    }
}

const char *StepperTest_StateText(StepperTestState_t state)
{
    switch (state) {
    case STEPPER_TEST_SAFE_RELEASED:     return "SAFE_RELEASED";
    case STEPPER_TEST_ZERO_READY:        return "ZERO_READY";
    case STEPPER_TEST_VISUAL_TRACKING:   return "VISUAL_TRACKING";
    case STEPPER_TEST_VISUAL_FAULT_HOLD: return "VISUAL_FAULT_HOLD";
    case STEPPER_TEST_ESTOP_LOCKED:      return "ESTOP_LOCKED";
    default:                             return "UNKNOWN";
    }
}
