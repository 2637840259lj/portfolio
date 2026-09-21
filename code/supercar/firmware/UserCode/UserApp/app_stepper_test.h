#ifndef APP_STEPPER_TEST_H
#define APP_STEPPER_TEST_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Hardware safety wrapper for the rewritten single ball controller.
 * The controller supplies one bounded desired pipe position. This module only
 * validates state, follows that target at a bounded speed, and owns K1/K2/K3.
 */
#define STEPPER_TEST_MODE_ENABLE 1

/* Set to 1 only while measuring the new linkage. The visual controller is
 * ignored; K1 zeroes, K2 moves a fixed increment, and K3 releases the motor. */
#define STEPPER_TRAVEL_CALIBRATION_MODE 0
#define STEPPER_CALIBRATION_INCREMENT_STEPS 10
#define STEPPER_CALIBRATION_STEP_HZ         12
#define STEPPER_CALIBRATION_DIRECTION       (-1)

/* New linkage measured from manually levelled zero: +231/-121 STEP. Keep
 * 5 STEP mechanical clearance; these are follower target guards, not TMC2209
 * driver soft limits. */
#define STEPPER_CONTROL_TARGET_MIN   (-116)
#define STEPPER_CONTROL_TARGET_MAX    226

/* Motor follower speed is independent of visual velocity. Use progressively
 * faster motion as target-position error grows: quiet near the requested pipe
 * angle, but retain the measured 250 STEP/s maximum for large corrections. */
#define STEPPER_FINE_STEP_HZ           80
#define STEPPER_SMALL_STEP_HZ         150
#define STEPPER_MEDIUM_STEP_HZ        200
#define STEPPER_LARGE_STEP_HZ         250
#define STEPPER_FINE_ERROR_STEPS        3
#define STEPPER_SMALL_ERROR_STEPS       8
#define STEPPER_MEDIUM_ERROR_STEPS     20

/* During a fast ball crossing, do not allow a freshly reversed visual request
 * to get arbitrarily far ahead of the real pipe position. This bounds the
 * command/plant mismatch while retaining rapid follower motion. */
#define STEPPER_FAST_BALL_SPEED_PX_S    80
#define STEPPER_FAST_TARGET_LEAD_STEPS  56

/* Internal control margin is tighter than the ±1cm scoring tolerance, so
 * camera/centroid error still leaves competition margin after settling. */
/* Once the ball has genuinely settled at center, retain the actual pipe
 * position instead of continually recalibrating slope from centroid noise.
 * Entry is deliberately stricter than the internal control band; exit uses
 * hysteresis so a useful push or drift restores full tracking immediately. */
#define STEPPER_CENTER_LOCK_ERROR_PX       21
#define STEPPER_CENTER_LOCK_SPEED_PX_S     12
#define STEPPER_CENTER_LOCK_HOLD_MS       300U
#define STEPPER_CENTER_UNLOCK_ERROR_PX     28
#define STEPPER_CENTER_UNLOCK_SPEED_PX_S   28

/* The tube is manually levelled before power-on, so auto-zero must not consume
 * the five-second competition budget. Five fresh vision frames still gate
 * actual motor motion after this brief hardware settling interval. */
#define STEPPER_AUTO_ZERO_DELAY_MS     300U
#define STEPPER_AUTO_REARM_GOOD_FRAMES   5U

typedef enum {
    STEPPER_TEST_SAFE_RELEASED = 0,
    STEPPER_TEST_ZERO_READY,
    STEPPER_TEST_VISUAL_TRACKING,
    STEPPER_TEST_VISUAL_FAULT_HOLD,
    STEPPER_TEST_ESTOP_LOCKED
} StepperTestState_t;

void StepperTest_Init(void);
void StepperTest_Task(void);
/* Runs at 10 ms. requested_steps is the controller output. error_px/vx/seq
 * drive endpoint hold/telemetry; stable_hold_target permits a low-speed,
 * in-tolerance target (O or final -5cm) to capture its actual pipe slope.
 * visual_ok must represent a fresh valid visual result. */
void StepperTest_UpdateVisualRequest(int16_t requested_steps, int16_t error_px,
                                     int16_t vx_px_s, uint32_t vision_seq,
                                     bool stable_hold_target, bool visual_ok);
void StepperTest_OnKey1(void);
void StepperTest_OnKey2(void);
void StepperTest_OnKey3Press(void);
bool StepperTest_IsExclusive(void);
StepperTestState_t StepperTest_GetState(void);
int16_t StepperTest_GetTargetSteps(void);
bool StepperTest_IsTuningSafe(void);
const char *StepperTest_StateText(StepperTestState_t state);

#endif /* APP_STEPPER_TEST_H */
