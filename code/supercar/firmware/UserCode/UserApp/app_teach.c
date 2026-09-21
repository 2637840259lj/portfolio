/**
 * @file    app_teach.c
 * @brief   示教编程 — 实现
 */

#include "app_teach.h"
#include "app_ble.h"
#include "ble_protocol.h"
#include "ble_param.h"
#include "encoder_driver.h"
#include "sys_time.h"
#include <string.h>

/* ---- 外部变量 ---- */
extern Encoder_t encoderLeft;
extern Encoder_t encoderRight;
extern float     g_ble_target_left;
extern float     g_ble_target_right;

/* ==================== 状态 ==================== */
typedef enum {
    TEACH_IDLE    = 0,
    TEACH_RECORD,
    TEACH_PLAYING,
    TEACH_PAUSED
} teach_state_t;

static teach_state_t  t_state     = TEACH_IDLE;
static uint8_t        t_rec_rate  = 20;
static uint32_t       t_rec_last  = 0;
static bool           t_recording = false;

/* 当前编辑/回放的序列 */
static waypoint_t     t_waypoints[MAX_WAYPOINTS];
static uint8_t        t_wp_count = 0;

/* 回放状态 */
static uint8_t  t_play_idx      = 0;
static uint8_t  t_play_speed    = 100;  /* 百分比, 100 = 原速 */
static int32_t  t_play_encL_start = 0;
static int32_t  t_play_encR_start = 0;
static uint32_t t_play_wp_start_ms = 0;

/* ==================== 实现 ==================== */

void AppTeach_Init(void)
{
    t_state     = TEACH_IDLE;
    t_wp_count  = 0;
    t_recording = false;
    memset(t_waypoints, 0, sizeof(t_waypoints));
}

/* ---------- 录制 ---------- */
void AppTeach_StartRecording(uint8_t rate_hz)
{
    t_state     = TEACH_RECORD;
    t_rec_rate  = (rate_hz >= 5 && rate_hz <= 50) ? rate_hz : 20;
    t_rec_last  = 0;
    t_recording = true;
    /* 清空航点 (新录制) */
    t_wp_count  = 0;

    g_ble_state = BLE_STATE_RECORD;
}

void AppTeach_StopRecording(void)
{
    t_recording = false;
    if (t_state == TEACH_RECORD)
        t_state = TEACH_IDLE;
    g_ble_state = BLE_STATE_IDLE;
}

void AppTeach_RecordWaypoint(void)
{
    if (t_wp_count >= MAX_WAYPOINTS) return;

    waypoint_t wp;
    memset(&wp, 0, sizeof(wp));
    wp.encL       = encoderLeft.total_count;
    wp.encR       = encoderRight.total_count;
    wp.speed      = (uint8_t)BLE_Param_GetBaseSpeed();
    wp.action     = 0x00; /* MOVE_TO */
    wp.timeout_ms = 5000;

    t_waypoints[t_wp_count++] = wp;
}

/* ---------- 航点管理 ---------- */
bool AppTeach_AddWaypoint(const waypoint_t *wp)
{
    if (t_wp_count >= MAX_WAYPOINTS) return false;
    memcpy(&t_waypoints[t_wp_count], wp, sizeof(waypoint_t));
    t_wp_count++;
    return true;
}

void AppTeach_ClearSequence(void)
{
    t_wp_count = 0;
    memset(t_waypoints, 0, sizeof(t_waypoints));
}

uint8_t AppTeach_GetWaypointCount(void)
{
    return t_wp_count;
}

uint8_t AppTeach_ListSequences(char names[4][16])
{
    /* 占位: 返回 0 个序列 (Flash 存储未实现) */
    uint8_t i;
    for (i = 0; i < 4; i++)
        names[i][0] = '\0';
    return 0;
}

/* ---------- 回放 ---------- */
void AppTeach_StartPlayback(uint8_t speed_scale)
{
    if (t_wp_count == 0) return;

    t_state           = TEACH_PLAYING;
    t_play_idx        = 0;
    t_play_speed      = (speed_scale >= 10 && speed_scale <= 200) ? speed_scale : 100;
    t_play_encL_start = encoderLeft.total_count;
    t_play_encR_start = encoderRight.total_count;
    t_play_wp_start_ms = mspm0_get_clock_ms();

    g_ble_state = BLE_STATE_PLAYING;
}

void AppTeach_Pause(void)
{
    if (t_state == TEACH_PLAYING) {
        t_state = TEACH_PAUSED;
        g_ble_target_left  = 0.0f;
        g_ble_target_right = 0.0f;
    }
}

void AppTeach_Resume(void)
{
    if (t_state == TEACH_PAUSED) {
        t_state = TEACH_PLAYING;
    }
}

void AppTeach_Abort(void)
{
    t_state = TEACH_IDLE;
    g_ble_target_left  = 0.0f;
    g_ble_target_right = 0.0f;
    g_ble_state = BLE_STATE_IDLE;
}

uint8_t AppTeach_GetProgress(void)
{
    if (t_wp_count == 0) return 0;
    if (t_play_idx >= t_wp_count) return 100;
    return (uint8_t)(((uint32_t)t_play_idx * 100) / t_wp_count);
}

/* ---------- 回放状态机 (10ms 主循环调用) ---------- */
bool AppTeach_PlaybackTask(void)
{
    if (t_state != TEACH_PLAYING) return false;
    if (t_play_idx >= t_wp_count) {
        /* 全部航点完成 */
        t_state = TEACH_IDLE;
        g_ble_target_left  = 0.0f;
        g_ble_target_right = 0.0f;
        g_ble_state = BLE_STATE_IDLE;

        /* 通知 App 回放完成 */
        uint8_t done = 1;
        BLE_Protocol_Send(BLE_CMD_TEACH_PLAY, &done, 1);
        return false;
    }

    waypoint_t *wp = &t_waypoints[t_play_idx];
    float speed_cm = (float)wp->speed * (float)t_play_speed / 100.0f;
    if (speed_cm < 1.0f) speed_cm = 1.0f;

    switch (wp->action) {

    case 0x00: /* MOVE_TO */
    {
        /* 计算当前编码器距离目标 */
        int32_t dL = wp->encL - t_play_encL_start - encoderLeft.total_count;
        int32_t dR = wp->encR - t_play_encR_start - encoderRight.total_count;

        /* 到达判定: 两个轮子编码器误差都在 ±50 以内 */
        if (dL > -50 && dL < 50 && dR > -50 && dR < 50) {
            /* 航点到达 */
            t_play_idx++;
            t_play_wp_start_ms = mspm0_get_clock_ms();

            /* 发送 ACK 给 App */
            BLE_Protocol_SendAck(BLE_CMD_TEACH_PLAY);
        } else {
            /* 继续行驶: 简单比例控制 */
            g_ble_target_left  = (float)dL * 0.02f + speed_cm;
            g_ble_target_right = (float)dR * 0.02f + speed_cm;

            /* 限幅 */
            if (g_ble_target_left  > speed_cm * 1.5f) g_ble_target_left  = speed_cm * 1.5f;
            if (g_ble_target_right > speed_cm * 1.5f) g_ble_target_right = speed_cm * 1.5f;
        }
        break;
    }

    case 0x01: /* TURN_L90 */
    case 0x02: /* TURN_R90 */
    {
        /* 原地转弯: 内轮停, 外轮动 */
        float turn_speed = BLE_Param_GetTurnOuter();
        if (wp->action == 0x02) { /* 右转 */
            g_ble_target_left  =  turn_speed;
            g_ble_target_right = -turn_speed * (BLE_Param_GetTurnInner() / BLE_Param_GetTurnOuter());
        } else { /* 左转 */
            g_ble_target_left  = -turn_speed * (BLE_Param_GetTurnInner() / BLE_Param_GetTurnOuter());
            g_ble_target_right =  turn_speed;
        }

        /* 编码器差值达到目标 → 完成 */
        int32_t dL = encoderLeft.total_count - t_play_encL_start;
        int32_t dR = encoderRight.total_count - t_play_encR_start;
        int32_t diff = (int32_t)BLE_Param_GetTurnTarget();

        /* 简化: 左轮右轮任一达到目标就完成 */
        if ((wp->action == 0x01 && dL < -diff) ||
            (wp->action == 0x02 && dL > diff)) {
            t_play_idx++;
            t_play_wp_start_ms = mspm0_get_clock_ms();
            g_ble_target_left  = 0.0f;
            g_ble_target_right = 0.0f;
        }
        break;
    }

    case 0x03: /* WAIT */
    {
        uint32_t elapsed = mspm0_get_clock_ms() - t_play_wp_start_ms;
        g_ble_target_left  = 0.0f;
        g_ble_target_right = 0.0f;
        if (elapsed >= wp->timeout_ms) {
            t_play_idx++;
            t_play_wp_start_ms = mspm0_get_clock_ms();
        }
        break;
    }

    case 0x04: /* LINE_TO — 循线段 */
    {
        /* 灰度循线: 依赖循迹 PID, 这里仅设基准速度 */
        g_ble_target_left  = speed_cm;
        g_ble_target_right = speed_cm;

        /* 编码器到达目标 */
        int32_t dL = wp->encL - t_play_encL_start - encoderLeft.total_count;
        int32_t dR = wp->encR - t_play_encR_start - encoderRight.total_count;
        if (dL > -100 && dL < 100) {
            t_play_idx++;
            t_play_wp_start_ms = mspm0_get_clock_ms();
        }
        break;
    }

    case 0x05: /* END */
    default:
        t_play_idx = t_wp_count; /* 强制结束 */
        break;
    }

    return true;
}
