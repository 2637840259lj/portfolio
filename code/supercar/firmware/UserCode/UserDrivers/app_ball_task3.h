#ifndef APP_BALL_TASK3_H
#define APP_BALL_TASK3_H

#include <stdbool.h>
#include <stdint.h>

#include "app_ball_vision.h"

/* K230 physical-coordinate calibration: O=377, +5cm=227, -5cm=522.
 * Positive physical distance maps to decreasing image x. */
#define BALL_TASK3_CENTER_X_PX          377
#define BALL_TASK3_PLUS_5CM_X_PX        227
#define BALL_TASK3_MINUS_5CM_X_PX       522
#define BALL_TASK3_ONE_CM_PX             30
#define BALL_TASK3_TARGET_TOLERANCE_PX  30
#define BALL_TASK3_STABLE_SPEED_PX_S     20
#define BALL_TASK3_STABLE_HOLD_MS       300U
#define BALL_TASK3_TIME_LIMIT_MS       5000U

/* Task 2 test: after O is stable, run O -> +5cm -> -5cm -> stable. */
#define BALL_TASK3_FULL_SEQUENCE_ENABLE 1

typedef enum {
    BALL_TASK3_HOLD_CENTER = 0,
    BALL_TASK3_WAIT_START,
    BALL_TASK3_GO_PLUS_5CM,
    BALL_TASK3_GO_MINUS_5CM,
    BALL_TASK3_COMPLETE_PASS,
    BALL_TASK3_COMPLETE_TIMEOUT
} BallTask3State_t;

typedef struct {
    BallTask3State_t state;
    int16_t target_x;
    uint32_t elapsed_ms;
    bool complete;
    bool within_time_limit;
} BallTask3Status_t;

void BallTask3_Init(void);
/* Competition scheduler owns task start/stop. Start time is the K2 command
 * time, so the five-second verdict includes O-point confirmation. */
void BallTask3_Start(uint32_t now_ms);
void BallTask3_Stop(void);
/* Query terminal verdict without advancing the state machine. */
bool BallTask3_IsComplete(bool *within_time_limit);
BallTask3Status_t BallTask3_Update(const BallPosition_t *ball, bool visual_ok,
                                   uint32_t now_ms);
const char *BallTask3_StateText(BallTask3State_t state);

#endif /* APP_BALL_TASK3_H */
