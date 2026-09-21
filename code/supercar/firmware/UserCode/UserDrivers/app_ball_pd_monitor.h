#ifndef APP_BALL_PD_MONITOR_H
#define APP_BALL_PD_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "app_ball_vision.h"

/*
 * Single trajectory controller for the ball. K230 supplies filtered x/vx;
 * this module converts them into one bounded pipe-position request. It never
 * accesses STEP, DIR, ENN, or a timer. The stepper layer owns all hardware and
 * safety interlocks.
 */
/* Physical center after K230 installation calibration. Do not reintroduce a
 * second nominal target here: all idle and movement-balance paths use 377. */
#define BALL_PD_PHYSICAL_BALANCE_X_PX   377
#define BALL_PD_DEADBAND_PX              10
#define BALL_PD_MIN_CONFIDENCE           70U
#define BALL_PD_VISION_TIMEOUT_MS       120U

/* New-linkage return trajectory. Limit the allowed ball speed before the
 * center and let the velocity term begin braking earlier; this reduces the
 * pipe reversal lag that previously produced >1000px/s crossings. */
#define BALL_PD_RETURN_VMAX_PX_S        155.0f
#define BALL_PD_RETURN_DECEL_PX_S2      310.0f
#define BALL_PD_RETURN_GUARD_PX          10
#define BALL_PD_VELOCITY_TO_DRIVE_DIV   2.40f

/* As a ball approaches the target at significant speed, increase only the
 * velocity damping gain. This is a continuous pre-brake, not a target switch,
 * so it removes energy before the ball crosses the center. */
#define BALL_PD_APPROACH_BRAKE_DISTANCE_PX 320
#define BALL_PD_APPROACH_BRAKE_SPEED_PX_S    55
#define BALL_PD_APPROACH_BRAKE_KD          0.46f
/* Terminal static-friction correction. It is enabled only at low speed. Inside
 * the ±1cm acceptance band it remains gentle; outside that band it guarantees
 * a measured minimum correction slope so a stopped ball cannot remain stuck. */
#define BALL_PD_CENTER_HOLD_KP          0.45f
#define BALL_PD_CENTER_HOLD_LIMIT       30.0f
#define BALL_PD_CENTER_HOLD_SPEED_PX_S   25
/* Use a ±0.7cm internal control margin; the task verdict remains ±1cm. */
#define BALL_PD_CENTER_ACCEPTANCE_PX     21
#define BALL_PD_CENTER_HOLD_MIN_STEPS   28.0f

/* Only for off-center competition targets: a small terminal position term
 * supplies the holding slope that the center-only +7 STEP bias cannot. */
#define BALL_PD_OFFCENTER_HOLD_KP       0.12f
#define BALL_PD_OFFCENTER_HOLD_LIMIT    18.0f

/* At a far, nearly stationary endpoint the long linkage needs more pipe
 * angle than the velocity trajectory alone supplies to overcome stiction.
 * Direction was measured: right-side ball needs negative STEP; left-side ball
 * needs positive STEP. These are intentional start-drive floors, not limits. */
#define BALL_PD_REMOTE_START_DISTANCE_PX 125
#define BALL_PD_REMOTE_START_SPEED_PX_S   55
#define BALL_PD_REMOTE_RIGHT_STEPS      (-60.0f)
#define BALL_PD_REMOTE_LEFT_STEPS         68.0f

/* Measured center equilibrium: x≈377 is held by +7 STEP. This installation
 * bias is center-only; applying it at ±5cm leaves a positive/upward slope even
 * when off-center position and velocity errors are both zero. */
#define BALL_PD_CENTER_HOLD_BIAS_STEPS    7.0f
#define BALL_PD_OFFCENTER_HOLD_BIAS_STEPS 0.0f
#define BALL_PD_COMMAND_TO_STEP           0.80f
#define BALL_PD_OUTPUT_LIMIT            125.0f
#define BALL_PD_REQUEST_STEP_MIN         (-90)
#define BALL_PD_REQUEST_STEP_MAX          100
#define BALL_PD_VELOCITY_LIMIT            900
/* Keep a damping reversal close enough to the actual pipe position that the
 * follower can realize it before the ball crosses the target. */
#define BALL_PD_DAMPING_STEP_LIMIT       42.0f

/* Motion-balance feed-forward. Positive STEP pushes the ball toward negative
 * image x. During normal forward travel the observed ball drift is backward;
 * rear is calibrated here as positive image x, so forward terms command a
 * negative STEP to pre-tilt toward positive x. If a takeoff log shows rear is
 * negative image x, change only BALL_MOTION_FORWARD_REAR_X_SIGN to -1. */
#define BALL_MOTION_FORWARD_REAR_X_SIGN   1.0f
#define BALL_MOTION_COMP_SPEED_GAIN       0.16f /* STEP / (cm/s) */
#define BALL_MOTION_COMP_ACCEL_GAIN       0.025f /* STEP / (cm/s^2) */
#define BALL_MOTION_COMP_DIFF_GAIN        0.00f /* STEP / (cm/s) */
/* yawRate<0 means right turn; right turn inertia shifts the ball to negative
 * x, therefore a positive gain correctly commands negative STEP toward +x. */
#define BALL_MOTION_COMP_YAWRATE_GAIN     0.05f /* STEP / (deg/s) */
#define BALL_MOTION_COMP_MAX_STEPS        18.0f

/* Kept as the guarded BLE pair. Kp scales planned return velocity into pipe
 * angle; Kd removes measured ball velocity. They are not a legacy P/D pair. */
#define BALL_PD_KP_DEFAULT               0.36f
#define BALL_PD_KD_DEFAULT               0.28f
#define BALL_PD_KP_MIN                   0.20f
#define BALL_PD_KP_MAX                   0.70f
#define BALL_PD_KD_MIN                   0.08f
#define BALL_PD_KD_MAX                   0.40f

typedef enum {
    BALL_PD_OK = 0,
    BALL_PD_NO_FRAME,
    BALL_PD_INVALID_FRAME,
    BALL_PD_LOW_CONFIDENCE,
    BALL_PD_TIMEOUT
} BallPdStatus_t;

typedef struct {
    float speed_avg_cm_s;
    float speed_diff_cm_s;
    float accel_cm_s2;
    float yaw_rate_dps;
    bool  imu_valid;
} BallMotionState_t;

typedef struct {
    BallPdStatus_t status;
    int16_t target_x;
    int16_t x;
    int16_t vx;
    int16_t vx_for_d;
    int16_t error_px;
    float p_term;      /* Planned return-velocity drive contribution. */
    float d_term;      /* Measured velocity damping contribution. */
    float command;
    int16_t requested_steps;
    uint32_t vision_age_ms;
    uint8_t confidence;
} BallPdMonitor_t;

void BallPdMonitor_Init(void);
/* Target x may be changed by the competition task state machine; x=377 is the
 * center-hold default. */
void BallPdMonitor_SetTargetX(int16_t target_x);
/* Applies only bounded movement feed-forward to a visual-PD step request.
 * It is valid only for explicit movement-balance missions. */
int16_t BallPdMonitor_ApplyMotionCompensation(int16_t pd_requested_steps,
                                               const BallMotionState_t *motion,
                                               bool enabled);
BallPdMonitor_t BallPdMonitor_Update(const BallPosition_t *ball, uint32_t vision_age_ms);
void BallPdMonitor_GetGains(float *kp, float *kd);
bool BallPdMonitor_SetGains(float kp, float kd);
const char *BallPdMonitor_StatusText(BallPdStatus_t status);

#endif /* APP_BALL_PD_MONITOR_H */
