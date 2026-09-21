/**
 * @file    app_ble_remote.c
 * @brief   BLE 遥控差速仲裁 — 实现
 *
 * @details
 *   从 empty.c 抽出。封装 BLE 实时遥控的 ramp + deadzone + failsafe，
 *   以及 Teach/AppIns 速度同步链路。ramp 状态本模块私有。
 *
 *   注意：AppTeach_PlaybackTask() 有副作用 (推进状态机)，每帧只能调一次，
 *   所以本模块不提供 IsActive 查询 —— 主循环在独占任务都不激活时直接调用
 *   AppBLE_RemoteDriveUpdate() 即可，内部自决是否写 targetSpeed*。
 */

#include "app_ble_remote.h"
#include "app_ble.h"
#include "app_teach.h"
#include "app_ins.h"
#include "ble.h"
#include "ble_param.h"
#include "sys_time.h"
#include <math.h>

/* ==================== 外部变量 ==================== */
extern float targetSpeedLeft;
extern float targetSpeedRight;

/* ==================== 模块私有状态 ==================== */
float g_left_ramp  = 0.0f;
float g_right_ramp = 0.0f;

#define BLE_RAMP_STEP        8.0f   /* 每帧(10ms)最大加速度 cm/s */
#define BLE_SPEED_DEADZONE   5      /* L/R 速度死区 (±5/100 = 5%) */
#define BLE_FAILSAFE_MS      500    /* 500ms 无命令 → 停车 */

/* ==================== API ==================== */

void AppBLE_RemoteReset(void)
{
    g_left_ramp  = 0.0f;
    g_right_ramp = 0.0f;
    BLE_ResetDrive();
}

void AppBLE_RemoteDriveUpdate(void)
{
    /* ---- 1. 路径示教回放接管速度控制 ---- */
    bool teach_active = AppTeach_PlaybackTask();
    if (teach_active) {
        targetSpeedLeft  = g_ble_target_left;
        targetSpeedRight = g_ble_target_right;
        return;
    }

    /* ---- 2. BLE 遥控: 直接读 L/R 差速值, 死区 + 加速度限制 + 失效保护 ---- */
    if (BLE_GetDriveActive() || BLE_GetLeft() != 0 || BLE_GetRight() != 0 ||
        fabsf(g_left_ramp) > 0.1f || fabsf(g_right_ramp) > 0.1f) {

        /* AppIns 执行期间若 BLE 遥控介入, 需暂停脚本以防 start_ms 过时触发超时 */
        if (AppIns_IsActive()) {
            AppIns_Pause();
        }

        uint32_t now_ms = mspm0_get_clock_ms();

        /* 失效保护: 500ms 无命令 → 停车 */
        if (now_ms - BLE_GetLastCmdMs() > BLE_FAILSAFE_MS) {
            BLE_ResetDrive();
        }

        /* 从 ble.c 直接读取 L/R 差速值 (App 已算好, MCU 不再算差速) */
        int lv = BLE_GetLeft();
        int rv = BLE_GetRight();
        /* 死区: 接近0视为0, 避免摇杆回中时微小抖动 */
        if (lv > -BLE_SPEED_DEADZONE && lv < BLE_SPEED_DEADZONE) lv = 0;
        if (rv > -BLE_SPEED_DEADZONE && rv < BLE_SPEED_DEADZONE) rv = 0;
        float base = BLE_Param_GetBaseSpeed();
        float tgtL = base * lv / 100.0f;
        float tgtR = base * rv / 100.0f;

        /* 加速度限制 */
        if (tgtL > g_left_ramp + BLE_RAMP_STEP)  g_left_ramp  += BLE_RAMP_STEP;
        else if (tgtL < g_left_ramp - BLE_RAMP_STEP) g_left_ramp -= BLE_RAMP_STEP;
        else g_left_ramp = tgtL;
        if (tgtR > g_right_ramp + BLE_RAMP_STEP)  g_right_ramp += BLE_RAMP_STEP;
        else if (tgtR < g_right_ramp - BLE_RAMP_STEP) g_right_ramp -= BLE_RAMP_STEP;
        else g_right_ramp = tgtR;

        targetSpeedLeft  = g_left_ramp;
        targetSpeedRight = g_right_ramp;
        return;
    }

    /* ---- 3. 指令解释器 (接管速度) ---- */
    AppIns_Task();
    if (AppIns_IsActive()) {
        targetSpeedLeft  = g_ble_target_left;
        targetSpeedRight = g_ble_target_right;
    }
}
