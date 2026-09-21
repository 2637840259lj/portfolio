#include "app_ball_task3.h"

static BallTask3State_t s_state;
static uint32_t s_start_ms;
static uint32_t s_stable_since_ms;

static int32_t abs_i32(int32_t value)
{
    return (value < 0) ? -value : value;
}

static bool at_target(const BallPosition_t *ball, int16_t target_x)
{
    if (ball == 0) return false;
    return abs_i32((int32_t)ball->x - target_x) <= BALL_TASK3_TARGET_TOLERANCE_PX;
}

static bool at_target_and_slow(const BallPosition_t *ball, int16_t target_x)
{
    return at_target(ball, target_x) &&
           (abs_i32((int32_t)ball->vx) <= BALL_TASK3_STABLE_SPEED_PX_S);
}

void BallTask3_Init(void)
{
    s_state = BALL_TASK3_HOLD_CENTER;
    s_start_ms = 0U;
    s_stable_since_ms = 0U;
}

void BallTask3_Start(uint32_t now_ms)
{
#if BALL_TASK3_FULL_SEQUENCE_ENABLE
    s_state = BALL_TASK3_WAIT_START;
    s_start_ms = now_ms;
    s_stable_since_ms = 0U;
#else
    (void)now_ms;
    s_state = BALL_TASK3_HOLD_CENTER;
    s_start_ms = 0U;
    s_stable_since_ms = 0U;
#endif
}

void BallTask3_Stop(void)
{
    s_state = BALL_TASK3_HOLD_CENTER;
    s_start_ms = 0U;
    s_stable_since_ms = 0U;
}

bool BallTask3_IsComplete(bool *within_time_limit)
{
    bool complete = (s_state == BALL_TASK3_COMPLETE_PASS) ||
                    (s_state == BALL_TASK3_COMPLETE_TIMEOUT);
    if (within_time_limit != 0) {
        *within_time_limit = (s_state == BALL_TASK3_COMPLETE_PASS);
    }
    return complete;
}

BallTask3Status_t BallTask3_Update(const BallPosition_t *ball, bool visual_ok,
                                   uint32_t now_ms)
{
    BallTask3Status_t status;
    int16_t target_x = BALL_TASK3_CENTER_X_PX;
    bool stable = false;

    if (s_state == BALL_TASK3_HOLD_CENTER) {
        status.state = s_state;
        status.target_x = BALL_TASK3_CENTER_X_PX;
        status.elapsed_ms = 0U;
        status.complete = false;
        status.within_time_limit = false;
        return status;
    }

    if ((s_state == BALL_TASK3_WAIT_START) &&
        (now_ms - s_start_ms > BALL_TASK3_TIME_LIMIT_MS)) {
        s_state = BALL_TASK3_COMPLETE_TIMEOUT;
        s_stable_since_ms = 0U;
    }

    if (s_state == BALL_TASK3_WAIT_START) {
        stable = visual_ok && at_target_and_slow(ball, BALL_TASK3_CENTER_X_PX);
        if (stable) {
            if (s_stable_since_ms == 0U) s_stable_since_ms = now_ms;
            if (now_ms - s_stable_since_ms >= BALL_TASK3_STABLE_HOLD_MS) {
                s_state = BALL_TASK3_GO_PLUS_5CM;
                s_stable_since_ms = 0U;
            }
        } else {
            s_stable_since_ms = 0U;
        }
    }

    if ((s_state == BALL_TASK3_GO_PLUS_5CM) ||
        (s_state == BALL_TASK3_GO_MINUS_5CM)) {
        uint32_t elapsed_ms = now_ms - s_start_ms;
        if (elapsed_ms > BALL_TASK3_TIME_LIMIT_MS) {
            s_state = BALL_TASK3_COMPLETE_TIMEOUT;
            s_stable_since_ms = 0U;
        }
    }

    if (s_state == BALL_TASK3_GO_PLUS_5CM) {
        target_x = BALL_TASK3_PLUS_5CM_X_PX;
        /* The +5cm point is a turnaround waypoint, not the final holding
         * requirement. Reverse on first entry into its ±1cm scoring band so
         * the 5-second budget is spent on motion and final -5cm settling. */
        if (visual_ok && at_target(ball, target_x)) {
            s_state = BALL_TASK3_GO_MINUS_5CM;
            s_stable_since_ms = 0U;
        }
    }

    if (s_state == BALL_TASK3_GO_MINUS_5CM) {
        target_x = BALL_TASK3_MINUS_5CM_X_PX;
        stable = visual_ok && at_target_and_slow(ball, target_x);
        if (stable) {
            if (s_stable_since_ms == 0U) s_stable_since_ms = now_ms;
            if (now_ms - s_stable_since_ms >= BALL_TASK3_STABLE_HOLD_MS) {
                s_state = BALL_TASK3_COMPLETE_PASS;
            }
        } else {
            s_stable_since_ms = 0U;
        }
    }

    if ((s_state == BALL_TASK3_COMPLETE_PASS) ||
        (s_state == BALL_TASK3_COMPLETE_TIMEOUT)) {
        target_x = BALL_TASK3_MINUS_5CM_X_PX;
    } else if (s_state == BALL_TASK3_GO_PLUS_5CM) {
        target_x = BALL_TASK3_PLUS_5CM_X_PX;
    } else if (s_state == BALL_TASK3_GO_MINUS_5CM) {
        target_x = BALL_TASK3_MINUS_5CM_X_PX;
    }

    status.state = s_state;
    status.target_x = target_x;
    status.elapsed_ms = (s_start_ms == 0U) ? 0U : (now_ms - s_start_ms);
    status.complete = (s_state == BALL_TASK3_COMPLETE_PASS) ||
                      (s_state == BALL_TASK3_COMPLETE_TIMEOUT);
    status.within_time_limit = (s_state == BALL_TASK3_COMPLETE_PASS) &&
                               (status.elapsed_ms <= BALL_TASK3_TIME_LIMIT_MS);
    return status;
}

const char *BallTask3_StateText(BallTask3State_t state)
{
    switch (state) {
    case BALL_TASK3_HOLD_CENTER: return "HOLD_CENTER";
    case BALL_TASK3_WAIT_START: return "WAIT_START";
    case BALL_TASK3_GO_PLUS_5CM: return "GO_PLUS_5CM";
    case BALL_TASK3_GO_MINUS_5CM: return "GO_MINUS_5CM";
    case BALL_TASK3_COMPLETE_PASS: return "COMPLETE_PASS";
    case BALL_TASK3_COMPLETE_TIMEOUT: return "COMPLETE_TIMEOUT";
    default: return "UNKNOWN";
    }
}
