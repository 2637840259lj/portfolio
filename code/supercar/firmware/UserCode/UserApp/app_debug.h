/**
 * @file      app_debug.h
 * @brief     调试模式 — 直行+转弯组合
 * @details
 *   Key2 (PB21): 启动/停止
 *   Key1 (PB0) 长按2s: 灰度白校准
 */
#ifndef __APP_DEBUG_H__
#define __APP_DEBUG_H__

#include <stdint.h>
#include <stdbool.h>

void  Debug_Init(void);
void  Debug_Start(void);
void  Debug_Stop(void);
void  Debug_Task(void);
bool  Debug_IsRunning(void);

#endif
