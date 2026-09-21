/**
 * @file    app_ble_ascii.c
 * @brief   BLE ASCII 协议命令处理 — 实现
 *
 * @details
 *   从 empty.c 抽出。承接所有 ASCII 命令的解析与分发，包括安全命令
 *   (STOP/BRK/EMG)、独占任务启动 (IMU_CAL/GRAY_CAL/LINE_START/SPEED_TUNE_START)、
 *   状态查询 (PING/GET_HW/MOTOR_STATUS/LINE_STATUS)、参数微调 (PPx/PDx/BZx)。
 *
 *   命令分发需要触发独占任务切换时，调用 empty.c 导出的 AbortBleExclusiveWork /
 *   GrayCalibrateStart，并读写 g_gray_cal_active / g_line_debug_active。
 */

#include "app_ble_ascii.h"
#include "app_ble.h"
#include "app_imu.h"
#include "app_ins.h"
#include "app_page.h"
#include "app_teach.h"
#include "app_template.h"
#include "ble.h"
#include "ble_param.h"
#include "ble_protocol.h"
#include "encoder_driver.h"
#include "No_Mcu_Ganv_Grayscale_Sensor.h"
#include "pid.h"
#include <string.h>

/* ==================== 外部变量 ==================== */
extern Encoder_t encoderLeft;
extern Encoder_t encoderRight;
extern PID_t     pidLeftSpeed;
extern PID_t     pidRightSpeed;
extern PID_t     pidLine;
extern float     targetSpeedLeft;
extern float     targetSpeedRight;
extern float     g_speed_tune_target;
extern No_MCU_Sensor g_GraySensor;
extern uint8_t   Get_Gray_Digital(void);

/* ==================== 内部辅助 ==================== */

static void write_i16_le(uint8_t *d, uint8_t off, int16_t v)
{
    d[off] = (uint8_t)(v & 0xFF); d[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}
static void write_i32_le(uint8_t *d, uint8_t off, int32_t v)
{
    d[off] = (uint8_t)(v & 0xFF); d[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    d[off + 2] = (uint8_t)((v >> 16) & 0xFF); d[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

/* ==================== MOTOR_STATUS 响应 ==================== */
/* encL/R:i32, speedL/R:i16(cm/s*10), targetL/R:i16,
 * PID Kp:i16(*100), Ki:i16(*1000), Kd/Kf:i16(*100) */
static void handle_motor_status(void)
{
    uint8_t resp[24];
    float sl = Encoder_GetSpeedLine(&encoderLeft);
    float sr = Encoder_GetSpeedLine(&encoderRight);
    write_i32_le(resp, 0, encoderLeft.total_count);
    write_i32_le(resp, 4, encoderRight.total_count);
    write_i16_le(resp, 8,  (int16_t)(sl * 10.0f));
    write_i16_le(resp, 10, (int16_t)(sr * 10.0f));
    write_i16_le(resp, 12, (int16_t)(targetSpeedLeft  * 10.0f));
    write_i16_le(resp, 14, (int16_t)(targetSpeedRight * 10.0f));
    write_i16_le(resp, 16, (int16_t)(pidLeftSpeed.kp * 100.0f));
    write_i16_le(resp, 18, (int16_t)(pidLeftSpeed.ki * 1000.0f));
    write_i16_le(resp, 20, (int16_t)(pidLeftSpeed.kd * 100.0f));
    write_i16_le(resp, 22, (int16_t)(pidLeftSpeed.kf * 100.0f));
    BLE_Protocol_Send(BLE_CMD_MOTOR_STATUS, resp, 24);
}

/* ==================== LINE_STATUS 响应 ==================== */
/* digital:u8, debugActive:u8, error:i16(*10), line Kp/Kd:i16(*100000) */
static void handle_line_status(void)
{
    uint8_t resp[8];
    float err = -CalculateNormalizedValue(&g_GraySensor, 0);
    resp[0] = Get_Gray_Digital();
    resp[1] = g_line_debug_active ? 1 : 0;
    write_i16_le(resp, 2, (int16_t)(err * 10.0f));
    write_i16_le(resp, 4, (int16_t)(pidLine.kp * 100000.0f));
    write_i16_le(resp, 6, (int16_t)(pidLine.kd * 100000.0f));
    BLE_Protocol_Send(BLE_CMD_LINE_STATUS, resp, 8);
}

/* ==================== 主分发 ==================== */
void AppBLE_AsciiHandle(void)
{
    if (!BLE_AsciiReady()) return;

    const char *cmd = BLE_GetCmd();

    if (strcmp(cmd, "STOP") == 0 || strcmp(cmd, "BRK") == 0) {
        /* S0 安全抢占：队列已在 BLE 层清空，这里取消当前独占任务并确认已停。 */
        AbortBleExclusiveWork();
        g_ble_remote_active = 0;
        BLE_Protocol_SendAck(strcmp(cmd, "STOP") == 0 ? BLE_CMD_STOP : BLE_CMD_BRAKE);
    }
    else if (strcmp(cmd, "EMG") == 0) {
        /* S0 急停：比所有任务优先，清运动状态、示教与指令执行。 */
        AbortBleExclusiveWork();
        g_ble_remote_active = 0;
        AppIns_Stop();
        AppTeach_Abort();
        BLE_Protocol_SendAck(BLE_CMD_EMERGENCY);
    }
    else if (strcmp(cmd, "PING") == 0) {
        /* 校准期间不生成新的低优先级 PING 回包，防止挤占最终 ACK[0x73]。 */
        if (g_ble_state != BLE_STATE_CALIB) {
            uint8_t resp[4] = { 1, 0, g_ble_state, 0 };
            BLE_Protocol_Send(BLE_CMD_PING, resp, 4);
        }
    }
    else if (strcmp(cmd, "GET_HW") == 0) {
        AppPage_HandleGetHWStatus();
    }
    else if (strcmp(cmd, "TELEM_ON") == 0) {
        AppBLE_SetTelemetry(true, 20);
        /* 让 App 能确认 MCU 已实际接收 TM+ 命令。 */
        BLE_Protocol_SendAck(BLE_CMD_TELEM_START);
    }
    else if (strcmp(cmd, "TELEM_OFF") == 0) {
        AppBLE_SetTelemetry(false, 0);
        BLE_Protocol_SendAck(BLE_CMD_TELEM_STOP);
    }
    else if (strcmp(cmd, "IMU_CAL") == 0) {
        /* S1 独占校准：非阻塞采样，期间仍能立即收 EMG/BRK。
         * 清除遗留 PING/状态/遥测回包，仅保留已产生的 ACK/NACK；
         * 校准完成的 ACK[0x73] 将以最高优先级立即进入 TX 队列。 */
        BLE_ClearSafetyLock();
        AbortBleExclusiveWork();
        AppBLE_SetTelemetry(false, 0);
        BLE_TxDropNonSafety();
        g_ble_remote_active = 0;
        g_ble_state = BLE_STATE_CALIB;
        if (!ImuApp_StartCalibrateGyro()) {
            g_ble_state = BLE_STATE_ERROR;
            BLE_Protocol_SendNack(BLE_CMD_IMU_CAL, BLE_ERR_BUSY);
        }
    }
    else if (strcmp(cmd, "YAW_ZERO") == 0) {
        ImuApp_ResetYaw();
        BLE_Protocol_SendAck(BLE_CMD_YAW_ZERO);
    }
    else if (strcmp(cmd, "MOTOR_STATUS") == 0) {
        handle_motor_status();
    }
    else if (strcmp(cmd, "SPEED_TUNE_START") == 0) {
        /* S1 独占整定：先抢占旧任务与查询造成的残留控制权，
         * 再复用与二进制 0x38 同一条 AppSpeedTune_Start 启动路径，
         * 避免 ASCII/二进制双实现漂移。 */
        BLE_ClearSafetyLock();
        AbortBleExclusiveWork();
        if (AppSpeedTune_Start(g_speed_tune_target)) {
            BLE_Protocol_SendAck(BLE_CMD_SPEED_TUNE);
        } else {
            BLE_Protocol_SendNack(BLE_CMD_SPEED_TUNE, BLE_ERR_BUSY);
        }
    }
    else if (strcmp(cmd, "SPEED_TUNE_STOP") == 0) {
        /* S0 取消：无论当前是什么独占任务，都回到安全静止。 */
        AbortBleExclusiveWork();
        BLE_Protocol_SendAck(BLE_CMD_SPEED_TUNE);
    }
    else if (strcmp(cmd, "LINE_STATUS") == 0) {
        handle_line_status();
    }
    else if (strcmp(cmd, "GRAY_CAL") == 0) {
        /* S1 独占灰度校准：分步采样，完成后才 ACK，期间 S0 可抢占。 */
        BLE_ClearSafetyLock();
        AbortBleExclusiveWork();
        GrayCalibrateStart();
    }
    else if (strcmp(cmd, "LINE_START") == 0) {
        /* S1 独占循迹：开始前抢占遥控/整定/校准控制权。 */
        BLE_ClearSafetyLock();
        AbortBleExclusiveWork();
        PID_SetTarget(&pidLine, 0.0f);
        g_line_debug_active = true;
        BLE_Protocol_SendAck(BLE_CMD_LINE_START);
    }
    else if (strcmp(cmd, "LINE_STOP") == 0) {
        AbortBleExclusiveWork();
        BLE_Protocol_SendAck(BLE_CMD_LINE_STOP);
    }
    else if (cmd[0] == 'P' && (cmd[1] == 'P' || cmd[1] == 'D') && cmd[2] != '\0') {
        /* PPx/PDx: 以8为中心的小步调整，范围 -8..+7；P步长0.0001，D步长0.00001。 */
        int16_t delta = (cmd[2] <= '9' ? cmd[2] - '0' : cmd[2] - 'A' + 10) - 8;
        float next = cmd[1] == 'P' ? pidLine.kp + delta * 0.0001f
                                   : pidLine.kd + delta * 0.00001f;
        if (BLE_Param_Write(cmd[1] == 'P' ? PARAM_PID_LINE_KP : PARAM_PID_LINE_KD, next))
            BLE_Protocol_SendAck(BLE_CMD_PID_ADJUST);
        else
            BLE_Protocol_SendNack(BLE_CMD_PID_ADJUST, BLE_ERR_BAD_PARAM);
    }
    else if (cmd[0] == 'B' && cmd[1] == 'Z' && cmd[2] != '\0') {
        int16_t delta = (cmd[2] <= '9' ? cmd[2] - '0' : cmd[2] - 'A' + 10) - 8;
        ImuApp_AdjustGyroBiasZ(delta);
    }

    BLE_AsciiClear();
}
