#ifndef __APP_BLE_H
#define __APP_BLE_H

/**
 * @file    app_ble.h
 * @brief   BLE 应用层 — 命令分发、遥测管理
 */

#include <stdint.h>
#include <stdbool.h>

/* MCU 工作状态 */
enum {
    BLE_STATE_IDLE     = 0x00,
    BLE_STATE_RUNNING  = 0x01,
    BLE_STATE_RECORD   = 0x02,
    BLE_STATE_PLAYING  = 0x03,
    BLE_STATE_ERROR    = 0x04,
    BLE_STATE_CALIB    = 0x05,
    BLE_STATE_INS_EXEC = 0x06,
    BLE_STATE_INS_PAUSED = 0x07
};

extern uint8_t g_ble_state;

/* 遥控模式标志: true 时 BLE 遥控接管速度控制 */
extern bool g_ble_remote_active;

/* 全局速度目标 (由 BLE 遥控或示教回放写入, ISR 读取) */
extern float g_ble_target_left;
extern float g_ble_target_right;

/* ==================== API ==================== */

void AppBLE_Init(void);

/**
 * 主循环 10ms 调用。
 * 检查收包 → 分发命令 → 更新遥测。
 */
void AppBLE_Task(void);

/**
 * 发送遥测帧到手机 App。
 * 内部检查遥测使能标志和速率, 仅在需要时发送。
 * @param now_ms  当前时间戳 (ms)
 */
void AppBLE_TelemTrySend(uint32_t now_ms);

/** 启动/停止 MCU→App 遥测。rate_hz 仅在 enabled=true 时使用，范围 5~50Hz。 */
void AppBLE_SetTelemetry(bool enabled, uint8_t rate_hz);

/**
 * 启动一次由二进制 PID 曲线命令触发的安全速度阶跃。
 * 仅在静止、无其它独占闭环时成功；结果仍以 BLE_CMD_SPEED_TUNE 回传。
 */
bool AppSpeedTune_Start(float target_speed_cm_s);

#endif /* __APP_BLE_H */
