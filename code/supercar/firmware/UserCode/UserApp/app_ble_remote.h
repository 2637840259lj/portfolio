#ifndef __APP_BLE_REMOTE_H
#define __APP_BLE_REMOTE_H

/**
 * @file    app_ble_remote.h
 * @brief   BLE 遥控差速仲裁 — 从 empty.c 抽出
 *
 * @details
 *   主循环 10ms 调用 AppBLE_RemoteDriveUpdate()，内部按以下顺序仲裁速度控制权：
 *     1. AppTeach 路径示教回放  → 写 targetSpeed* (用户已停用, 保留兼容)
 *     2. BLE 实时遥控 L/R 差速  → ramp + deadzone + failsafe → 写 targetSpeed*
 *     3. AppIns 指令示教        → AppIns_Task + 同步 g_ble_target_* → targetSpeed*
 *
 *   ramp 状态 (g_left_ramp/g_right_ramp) 封装在本模块内，外部通过
 *   AppBLE_RemoteReset() 清零 (供 AbortBleExclusiveWork / 独占任务退出时调用)。
 */

/* ==================== API ==================== */

/**
 * 主循环 10ms 调用：仲裁 Teach / BLE 遥控 / AppIns 三者谁接管速度。
 * 内部直接写 targetSpeedLeft / targetSpeedRight。
 * 应在所有独占任务 (Competition/Bench/SpeedTune/LineDebug/GrayCalib) 都未激活时调用。
 */
void AppBLE_RemoteDriveUpdate(void);

/**
 * 清零 ramp 状态 + BLE 遥控缓冲。
 * 供 AbortBleExclusiveWork、SpeedTune_Stop、LineDebug 退出等场景调用，
 * 防止下一帧 BLE 遥控以旧 ramp 值复活。
 */
void AppBLE_RemoteReset(void);

#endif /* __APP_BLE_REMOTE_H */
