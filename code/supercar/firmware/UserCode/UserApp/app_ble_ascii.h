#ifndef __APP_BLE_ASCII_H
#define __APP_BLE_ASCII_H

#include <stdbool.h>

/**
 * @file    app_ble_ascii.h
 * @brief   BLE ASCII 协议命令处理 — 从 empty.c 抽出
 *
 * @details
 *   主循环 10ms 调用 AppBLE_AsciiHandle()，内部完成 ASCII 命令解析与分发：
 *     STOP / BRK / EMG / PING / GET_HW / TELEM_ON / TELEM_OFF / IMU_CAL /
 *     YAW_ZERO / MOTOR_STATUS / SPEED_TUNE_START / SPEED_TUNE_STOP /
 *     LINE_STATUS / GRAY_CAL / LINE_START / LINE_STOP / PPx / PDx / BZx
 *
 *   需要调用 empty.c 的独占任务管理函数，这些函数和状态由 empty.c 导出：
 *     - AbortBleExclusiveWork()  统一收尾独占任务
 *     - GrayCalibrateStart()     启动灰度白校准
 *     - g_gray_cal_active        灰度校准进行中标志
 *     - g_line_debug_active      循迹台架进行中标志
 */

/* ==================== empty.c 导出 (供本模块调用) ==================== */
void AbortBleExclusiveWork(void);
void GrayCalibrateStart(void);
extern bool g_gray_cal_active;
extern bool g_line_debug_active;

/* ==================== API ==================== */

/**
 * 主循环 10ms 调用：检查 BLE ASCII 命令就绪并分发。
 * 内部完成 BLE_AsciiReady() 检查、命令分发、BLE_AsciiClear() 收尾。
 */
void AppBLE_AsciiHandle(void);

#endif /* __APP_BLE_ASCII_H */
