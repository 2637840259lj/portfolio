/**
 * @file      app_position_hold.c
 * @brief     电机 PD 位置保持 — 纯PD无积分
 * @details
 *   独立模块, 零外部依赖 (不含任何其他模块的 include)。
 *
 *   控制律:
 *     err  = target - current
 *     dErr = err - last_err
 *     out  = Kp * err + Kd * dErr
 *     if |err| < deadband → out = 0
 *     out = clamp(out, -max_output, +max_output)
 *
 *   典型场景: 小车被外力推离原位 → PD 产生反向力矩推回去。
 */
#include "app_position_hold.h"
#include <stdlib.h>   /* abs */

void PosHold_Init(PosHold_t *hold, float kp, float kd,
                  int16_t deadband, int16_t max_output)
{
    hold->enabled    = false;
    hold->target_pos = 0;
    hold->last_error = 0;
    hold->kp         = kp;
    hold->kd         = kd;
    hold->deadband   = deadband;
    hold->max_output = (max_output > 0) ? max_output : (int16_t)(-max_output);
}

void PosHold_Enable(PosHold_t *hold, int32_t current_pos)
{
    hold->target_pos = current_pos;
    hold->last_error = 0;
    hold->enabled    = true;
}

void PosHold_Disable(PosHold_t *hold)
{
    hold->enabled    = false;
    hold->target_pos = 0;
    hold->last_error = 0;
}

int16_t PosHold_Update(PosHold_t *hold, int32_t current_pos)
{
    if (!hold->enabled) {
        hold->last_error = 0;
        return 0;
    }

    int32_t err  = hold->target_pos - current_pos;
    int32_t dErr = err - hold->last_error;
    hold->last_error = err;

    /* 死区: 误差太小不动作, 避免 PWM 抖动 */
    if (abs(err) <= hold->deadband) {
        return 0;
    }

    /* PD 计算 */
    float output = hold->kp * (float)err + hold->kd * (float)dErr;

    /* 限幅 */
    if (output > (float)hold->max_output)  output = (float)hold->max_output;
    if (output < -(float)hold->max_output) output = -(float)hold->max_output;

    return (int16_t)output;
}
