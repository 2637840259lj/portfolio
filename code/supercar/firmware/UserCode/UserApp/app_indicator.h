/**
 * @file      app_indicator.h
 * @brief     LED + 蜂鸣器指示器 (竞赛版)
 */
#ifndef __APP_INDICATOR_H__
#define __APP_INDICATOR_H__

#include <stdbool.h>
#include <stdint.h>

void App_IndicatorTask(void);
void App_Indicator_RequestBeepOnce(void);
void App_Indicator_RequestBeepCount(uint8_t count);
void App_Indicator_SetMissionRunning(bool is_running);
void App_Indicator_SetRedBlink(bool enable);

/**
 * @brief 蜂鸣长鸣 (用于任务完成提示)
 */
void App_Indicator_RequestLongBeep(void);

/**
 * @brief 显示当前选中的任务编号 (LED颜色常亮)
 *        任务1=红 任务2=绿 任务3=蓝 任务4=紫
 * @param task_id 任务编号 1~4
 */
void App_Indicator_ShowTask(uint8_t task_id);

/**
 * @brief 设置空闲状态下显示的任务颜色 (任务切换时调用)
 * @param task_id 1=红 2=绿 3=蓝 4=紫
 */
void App_Indicator_SetSelectedTask(uint8_t task_id);

/**
 * @brief 上电自检序列 (RGB各闪一次+蜂鸣)
 */
void App_Indicator_PowerOnSelfTest(void);

/**
 * @brief IMU初始化结果提示
 * @param ok true=成功(蓝灯+1声), false=失败(红灯闪3次+3声)
 */
void App_Indicator_ImuResult(bool ok);

#endif
