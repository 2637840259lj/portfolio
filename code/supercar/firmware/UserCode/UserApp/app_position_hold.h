/**
 * @file      app_position_hold.h
 * @brief     电机 PD 位置保持 (无积分I)
 * @details
 *   独立模块, 不依赖任何外部PID库。
 *
 *   用法:
 *     1. PosHold_Init() 初始化
 *     2. PosHold_Enable(&hold, encoder.total_count) → 锁定当前位置
 *     3. 每个控制周期调用 PosHold_Update(&hold, encoder.total_count)
 *        → 返回 PWM 值, 正值=正向纠偏, 负值=反向纠偏
 *     4. PosHold_Disable(&hold) → 释放, PWM=0
 *
 *   原理:
 *     err = target - current
 *     derivative = err - last_err
 *     output = Kp * err + Kd * derivative
 *     |err| < deadband → output = 0 (防抖动)
 */
#ifndef __APP_POSITION_HOLD_H__
#define __APP_POSITION_HOLD_H__

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool     enabled;         /* 是否启用保持 */
    int32_t  target_pos;      /* 锁定时的目标位置(编码器脉冲) */
    int32_t  last_error;      /* 上一周期误差 (用于D项) */
    float    kp;              /* 比例系数 (脉冲→PWM) */
    float    kd;              /* 微分系数 (脉冲/s→PWM) */
    int16_t  deadband;        /* 死区(脉冲), |err|<此值不输出 */
    int16_t  max_output;      /* PWM输出限幅 (绝对值) */
} PosHold_t;

/**
 * @brief 初始化位置保持器
 * @param hold       保持器指针
 * @param kp         比例系数, 建议 0.5~2.0
 * @param kd         微分系数, 建议 0.1~0.5
 * @param deadband   死区(脉冲), 建议 3~5
 * @param max_output PWM限幅, 建议 500~999
 */
void PosHold_Init(PosHold_t *hold, float kp, float kd,
                  int16_t deadband, int16_t max_output);

/**
 * @brief 启用位置保持 (以当前编码器值为目标)
 * @param hold        保持器指针
 * @param current_pos 当前编码器位置(脉冲)
 */
void PosHold_Enable(PosHold_t *hold, int32_t current_pos);

/**
 * @brief 关闭位置保持 (电机自由)
 * @param hold 保持器指针
 */
void PosHold_Disable(PosHold_t *hold);

/**
 * @brief 更新位置保持 (每控制周期调用一次)
 * @param hold        保持器指针
 * @param current_pos 当前编码器位置(脉冲)
 * @return PWM输出值 (-max_output ~ +max_output)
 *         正值=正向纠偏, 负值=反向纠偏, 0=死区内无需纠偏
 */
int16_t PosHold_Update(PosHold_t *hold, int32_t current_pos);

/**
 * @brief 是否启用
 */
static inline bool PosHold_IsEnabled(const PosHold_t *hold)
{
    return hold->enabled;
}

#endif
