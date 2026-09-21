/**
 * @file    app_ble.c
 * @brief   BLE 应用层 — 命令分发与遥测管理
 *
 * 从 ble_protocol 接收完整包, 按命令码分发给对应处理函数。
 * 管理遥测推送 (定时向手机发送编码器/IMU 数据)。
 */

#include "app_ble.h"
#include "ble.h"
#include "ble_protocol.h"
#include "ble_param.h"
#include "ble_debug.h"
#include "app_teach.h"
#include "app_ins.h"
#include "app_page.h"
#include "app_template.h"
#include "app_competition.h"
#include "app_ball_vision.h"
#include "app_ball_pd_monitor.h"
#include "app_stepper_test.h"
#include "tmc2209_driver.h"
#include "encoder_driver.h"
#include "pid.h"
#include "app_imu.h"
#include "No_Mcu_Ganv_Grayscale_Sensor.h"
#include "sys_time.h"
#include <stdio.h>
#include <string.h>

/* ---- 全局状态 ---- */
uint8_t g_ble_state         = BLE_STATE_IDLE;
bool    g_ble_remote_active  = false;
float   g_ble_target_left    = 0.0f;
float   g_ble_target_right   = 0.0f;

/* ---- 遥测 ---- */
static bool     telem_enabled  = false;
static uint8_t  telem_rate_hz  = 20;
static uint32_t telem_last_ms  = 0;
/* Separate ball telemetry is deliberately rate-limited to protect ACK and
 * emergency-stop delivery on the CH9141 UART transport. */
static uint32_t ball_telem_last_ms = 0;
#define BALL_TELEM_RATE_HZ 20U
#define BALL_TUNE_PAYLOAD_LEN 8U
#define BALL_TUNE_SAFE_ERROR_PX 32

/* ---- 外部变量 ---- */
extern Encoder_t encoderLeft;
extern Encoder_t encoderRight;
extern No_MCU_Sensor g_GraySensor;
extern float targetSpeedLeft;
extern float targetSpeedRight;
extern PID_t pidLeftSpeed;
extern PID_t pidRightSpeed;
extern PID_t pidLine;
extern float g_speed_tune_target;
/* H题 App 实时循迹调试模式：定义于empty.c，管理OLED/IMU的I2C0分配。 */
extern bool g_h1_app_debug_mode;
extern bool g_imu_enabled;
/* 定义于empty.c：H题离线标定采样状态机，完成结果由主循环异步发送CAL_RESULT。 */
extern uint8_t H1CalibrationCaptureStart(uint8_t kind, uint8_t method);

/* ---- 内部辅助 ---- */
static void send_ack(uint8_t cmd)  { BLE_Protocol_SendAck(cmd); }
static void send_nack(uint8_t cmd, uint8_t err) { BLE_Protocol_SendNack(cmd, err); }

static uint8_t read_u8(const uint8_t *d, uint8_t off)  { return d[off]; }
static int16_t read_i16(const uint8_t *d, uint8_t off) { return (int16_t)((uint16_t)d[off] | ((uint16_t)d[off+1] << 8)); }
static int32_t read_i32(const uint8_t *d, uint8_t off) { return (int32_t)((uint32_t)d[off] | ((uint32_t)d[off+1] << 8) | ((uint32_t)d[off+2] << 16) | ((uint32_t)d[off+3] << 24)); }
static float   read_f32(const uint8_t *d, uint8_t off) { uint32_t u = read_i32(d, off); float f; memcpy(&f, &u, 4); return f; }

static void write_i16(uint8_t *d, uint8_t off, int16_t v) { d[off]=v&0xFF; d[off+1]=(v>>8)&0xFF; }
static void write_i32(uint8_t *d, uint8_t off, int32_t v) { d[off]=v&0xFF; d[off+1]=(v>>8)&0xFF; d[off+2]=(v>>16)&0xFF; d[off+3]=(v>>24)&0xFF; }
static void write_u32(uint8_t *d, uint8_t off, uint32_t v) { write_i32(d, off, (int32_t)v); }
static void write_f32(uint8_t *d, uint8_t off, float v)   { uint32_t u; memcpy(&u, &v, 4); write_i32(d, off, (int32_t)u); }

static uint8_t xor8_bytes(const uint8_t *data, uint8_t len)
{
    uint8_t result = 0U;
    uint8_t i;
    for (i = 0U; i < len; i++) result ^= data[i];
    return result;
}

/* PID 曲线命令仅允许小车静止且没有任何 BLE 运动控制权时执行。
 * 防止阶跃测试、循迹或遥控运行中被一条写参命令打断。 */
static bool pid_schedule_write_allowed(void)
{
    return g_ble_state == BLE_STATE_IDLE && !g_ble_remote_active &&
           targetSpeedLeft > -0.1f && targetSpeedLeft < 0.1f &&
           targetSpeedRight > -0.1f && targetSpeedRight < 0.1f;
}

static bool read_schedule_node(const uint8_t *d, uint8_t len, pid_schedule_node_t *node)
{
    if (d == 0 || node == 0 || len < 77U) return false;
    node->speed_cm_s = read_f32(d, 1);
    node->speed_kp = read_f32(d, 5); node->speed_ki = read_f32(d, 9);
    node->speed_kd = read_f32(d, 13); node->speed_kf = read_f32(d, 17);
    node->line_kp = read_f32(d, 21); node->line_kd = read_f32(d, 25);
    node->line_max_steer = read_f32(d, 29);
    node->curve_kp = read_f32(d, 33); node->curve_kd = read_f32(d, 37);
    node->curve_max_steer = read_f32(d, 41); node->curve_error_trigger = read_f32(d, 45);
    node->yaw_kp = read_f32(d, 49); node->yaw_kd = read_f32(d, 53);
    node->yaw_max_steer = read_f32(d, 57);
    node->pos_kp = read_f32(d, 61); node->decel_pulses = read_f32(d, 65);
    node->end_speed = read_f32(d, 69); node->distance_scale = read_f32(d, 73);
    return true;
}

static void write_schedule_node(uint8_t *d, uint8_t index, const pid_schedule_node_t *node)
{
    d[0] = index;
    write_f32(d, 1, node->speed_cm_s); write_f32(d, 5, node->speed_kp);
    write_f32(d, 9, node->speed_ki); write_f32(d, 13, node->speed_kd);
    write_f32(d, 17, node->speed_kf); write_f32(d, 21, node->line_kp);
    write_f32(d, 25, node->line_kd); write_f32(d, 29, node->line_max_steer);
    write_f32(d, 33, node->curve_kp); write_f32(d, 37, node->curve_kd);
    write_f32(d, 41, node->curve_max_steer); write_f32(d, 45, node->curve_error_trigger);
    write_f32(d, 49, node->yaw_kp); write_f32(d, 53, node->yaw_kd);
    write_f32(d, 57, node->yaw_max_steer); write_f32(d, 61, node->pos_kp);
    write_f32(d, 65, node->decel_pulses); write_f32(d, 69, node->end_speed);
    write_f32(d, 73, node->distance_scale);
}

/* ==================== 初始化 ==================== */
void AppBLE_SetTelemetry(bool enabled, uint8_t rate_hz)
{
    if (!enabled) {
        telem_enabled = false;
        telem_last_ms = 0;
        BLE_TxDropTelemetry();
        return;
    }
    if (rate_hz < 5) rate_hz = 5;
    if (rate_hz > 50) rate_hz = 50;
    /* 幂等启动：重复 TM+ 不重新清零计时器，避免同一时刻连续制造遥测帧。 */
    if (!telem_enabled || telem_rate_hz != rate_hz) telem_last_ms = 0;
    telem_rate_hz = rate_hz;
    telem_enabled = true;
}

void AppBLE_Init(void)
{
    g_ble_state        = BLE_STATE_IDLE;
    g_ble_remote_active = false;
    g_ble_target_left   = 0.0f;
    g_ble_target_right  = 0.0f;
    telem_enabled       = false;
    telem_rate_hz       = 20;
    telem_last_ms       = 0;

    BLE_Param_Init();
    BLE_Protocol_Init();
    BLE_Protocol_SetClock(mspm0_get_clock_ms);
    AppTeach_Init();
}

/* ==================== 主任务 ==================== */
void AppBLE_Task(void)
{
    if (!BLE_Protocol_Poll()) return;

    const ble_packet_t *pkt = BLE_Protocol_GetPacket();
    const uint8_t *d = pkt->data;
    uint8_t len = pkt->len;


    switch (pkt->cmd) {

    /* ===== 系统 ===== */
    case BLE_CMD_PING: {
        /* 校准期间忽略二进制 PING 回包，避免持续低优先级回传占满 TX 队列。 */
        if (g_ble_state != BLE_STATE_CALIB) {
            uint8_t resp[4] = { 1, 0, g_ble_state, 0 };
            BLE_Protocol_Send(BLE_CMD_PING, resp, 4);
        }
        break;
    }
    case BLE_CMD_RESET:
        send_ack(BLE_CMD_RESET);
        /* 延时 100ms 后软复位 */
        mspm0_delay_ms(100);
        NVIC_SystemReset();
        break;
    case BLE_CMD_GET_STATE: {
        uint8_t resp[2] = { g_ble_state, 0x00 };
        BLE_Protocol_Send(BLE_CMD_GET_STATE, resp, 2);
        break;
    }
    case BLE_CMD_GET_HW_STATUS:
        AppPage_HandleGetHWStatus();
        break;
    case BLE_CMD_GET_PAGE_DATA:
        if (len >= 1) {
            AppPage_HandleGetPageData(d[0]);
        }
        break;

    /* ===== 运动控制 ===== */
    case BLE_CMD_MOVE_RAW:
        if (len >= 4) {
            int16_t spdL = read_i16(d, 0);
            int16_t spdR = read_i16(d, 2);
            g_ble_target_left   = (float)spdL;
            g_ble_target_right  = (float)spdR;
            g_ble_remote_active = true;
            if (g_ble_state == BLE_STATE_IDLE)
                g_ble_state = BLE_STATE_RUNNING;
            send_ack(BLE_CMD_MOVE_RAW);
        }
        break;
    case BLE_CMD_STOP:
        /* 二进制安全停止也必须退出台架闭环，不能仅清遥控目标。 */
        AppBench_Abort();
        g_ble_target_left   = 0.0f;
        g_ble_target_right  = 0.0f;
        g_ble_remote_active = false;
        g_ble_state         = BLE_STATE_IDLE;
        send_ack(BLE_CMD_STOP);
        break;
    case BLE_CMD_EMERGENCY:
        AppBench_Abort();
        g_ble_target_left   = 0.0f;
        g_ble_target_right  = 0.0f;
        g_ble_remote_active = false;
        g_ble_state         = BLE_STATE_IDLE;
        send_ack(BLE_CMD_EMERGENCY);
        break;
    case BLE_CMD_SET_ARCADE:
        if (len >= 2) {
            int8_t throttle = (int8_t)d[0];
            int8_t steer    = (int8_t)d[1];
            float base  = BLE_Param_GetBaseSpeed();
            float spd   = ((float)throttle / 127.0f) * base * 2.0f; /* ±2×base */
            float diff  = ((float)steer / 127.0f) * base;           /* 转向差速 */
            g_ble_target_left   = spd + diff;
            g_ble_target_right  = spd - diff;
            g_ble_remote_active = (throttle != 0 || steer != 0);
            if (g_ble_state == BLE_STATE_IDLE && g_ble_remote_active)
                g_ble_state = BLE_STATE_RUNNING;
        }
        break;
    case BLE_CMD_SET_BASE_SPEED:
        if (len >= 1) {
            BLE_Param_Write(PARAM_BASE_SPEED, (float)d[0]);
            send_ack(BLE_CMD_SET_BASE_SPEED);
        }
        break;
    case BLE_CMD_TURN:
        /* 相对角度转弯: angle(i16) degree, speed(u8) cm/s */
        if (len >= 3) {
            int16_t angle = read_i16(d, 0);
            uint8_t speed = d[2];
            /* 异步转弯: 设差速, 不做闭环等待 (后续指令可替代) */
            if (angle > 0) {
                g_ble_target_left  = -(float)speed * 0.5f;
                g_ble_target_right =  (float)speed;
            } else {
                g_ble_target_left  =  (float)speed;
                g_ble_target_right = -(float)speed * 0.5f;
            }
            g_ble_remote_active = true;
            send_ack(BLE_CMD_TURN);
        }
        break;
    case BLE_CMD_MOVE_DIST:
        /* 定距直行: dist_cm(i16), speed(u8) */
        if (len >= 3) {
            int16_t dist = read_i16(d, 0);
            uint8_t speed = d[2];
            g_ble_target_left  = (float)speed;
            g_ble_target_right = (float)speed;
            g_ble_remote_active = true;
            send_ack(BLE_CMD_MOVE_DIST);
            (void)dist; /* TODO: 编码器距离闭环 */
        }
        break;
    case BLE_CMD_LINE_FOLLOW:
        /* 循线: enable(u8) 0=关 1=开 */
        if (len >= 1) {
            if (d[0]) {
                float base = BLE_Param_GetBaseSpeed();
                g_ble_target_left  = base;
                g_ble_target_right = base;
                g_ble_remote_active = true;
            } else {
                g_ble_target_left  = 0.0f;
                g_ble_target_right = 0.0f;
                g_ble_remote_active = false;
            }
            send_ack(BLE_CMD_LINE_FOLLOW);
        }
        break;
    case BLE_CMD_THROTTLE_SET:
        /* 油门设定: speed(u8) 0~80 cm/s */
        if (len >= 1) {
            float spd = (float)d[0];
            g_ble_target_left  = spd;
            g_ble_target_right = spd;
            g_ble_remote_active = (spd > 0);
            BLE_Param_Write(PARAM_BASE_SPEED, spd);
            send_ack(BLE_CMD_THROTTLE_SET);
        }
        break;
    case BLE_CMD_BRAKE:
        /* 刹车: 快速减速到 0，并退出任何台架闭环。 */
        AppBench_Abort();
        g_ble_target_left  = 0;
        g_ble_target_right = 0;
        g_ble_remote_active = false;
        send_ack(BLE_CMD_BRAKE);
        break;

    /* ===== 参数读写 ===== */
    case BLE_CMD_PARAM_READ:
        if (len >= 1) {
            float val = BLE_Param_Read(d[0]);
            uint8_t resp[5] = {0};
            resp[0] = d[0];
            write_f32(resp, 1, val);
            BLE_Protocol_Send(BLE_CMD_PARAM_READ, resp, 5);
        }
        break;
    case BLE_CMD_PARAM_WRITE:
        if (len >= 5) {
            uint8_t id = d[0];
            float val  = read_f32(d, 1);
            if (BLE_Param_Write(id, val))
                send_ack(BLE_CMD_PARAM_WRITE);
            else
                send_nack(BLE_CMD_PARAM_WRITE, BLE_ERR_BAD_PARAM);
        }
        break;
    case BLE_CMD_PARAM_SAVE:
        if (BLE_Param_SaveToFlash())
            send_ack(BLE_CMD_PARAM_SAVE);
        else
            send_nack(BLE_CMD_PARAM_SAVE, BLE_ERR_FLASH);
        break;
    case BLE_CMD_PID_SCHEDULE_READ:
        if (len >= 1 && d[0] < PID_SCHEDULE_NODE_COUNT) {
            uint8_t resp[77];
            write_schedule_node(resp, d[0], &BLE_Param_GetPidSchedule()[d[0]]);
            BLE_Protocol_Send(BLE_CMD_PID_SCHEDULE_READ, resp, sizeof(resp));
        } else {
            send_nack(BLE_CMD_PID_SCHEDULE_READ, BLE_ERR_BAD_PARAM);
        }
        break;
    case BLE_CMD_PID_SCHEDULE_WRITE:
        if (!pid_schedule_write_allowed()) {
            send_nack(BLE_CMD_PID_SCHEDULE_WRITE, BLE_ERR_BUSY);
        } else if (len >= 77 && d[0] < PID_SCHEDULE_NODE_COUNT) {
            pid_schedule_node_t node;
            if (read_schedule_node(d, len, &node) && BLE_Param_WritePidScheduleNode(d[0], &node))
                send_ack(BLE_CMD_PID_SCHEDULE_WRITE);
            else
                send_nack(BLE_CMD_PID_SCHEDULE_WRITE, BLE_ERR_BAD_PARAM);
        } else {
            send_nack(BLE_CMD_PID_SCHEDULE_WRITE, BLE_ERR_BAD_PARAM);
        }
        break;
    case BLE_CMD_PID_SCHEDULE_COMMIT:
        if (!pid_schedule_write_allowed()) {
            send_nack(BLE_CMD_PID_SCHEDULE_COMMIT, BLE_ERR_BUSY);
        } else if (BLE_Param_CommitPidSchedule()) {
            send_ack(BLE_CMD_PID_SCHEDULE_COMMIT);
        } else {
            send_nack(BLE_CMD_PID_SCHEDULE_COMMIT, BLE_ERR_BAD_PARAM);
        }
        break;
    case BLE_CMD_PID_SCHEDULE_AUTOTUNE:
        if (!pid_schedule_write_allowed() || len < 4) {
            send_nack(BLE_CMD_PID_SCHEDULE_AUTOTUNE, BLE_ERR_BUSY);
        } else {
            float test_speed = read_f32(d, 0);
            if (test_speed < 10.0f || test_speed > 50.0f) {
                send_nack(BLE_CMD_PID_SCHEDULE_AUTOTUNE, BLE_ERR_BAD_PARAM);
            } else if (AppSpeedTune_Start(test_speed)) {
                /* 二进制整定命令必须真正启动安全阶跃，不能只写目标速度
                 * 再依赖第二条 ASCII AT+；否则 App 收到 ACK 却看似无反应。 */
                send_ack(BLE_CMD_PID_SCHEDULE_AUTOTUNE);
            } else {
                /* 静止/独占互锁未通过 —— 告知 App 具体原因，而非静默 ACK。 */
                send_nack(BLE_CMD_PID_SCHEDULE_AUTOTUNE, BLE_ERR_BUSY);
            }
        }
        break;
    case BLE_CMD_BENCH_START:
        if (!pid_schedule_write_allowed() || len < 2) {
            send_nack(BLE_CMD_BENCH_START, BLE_ERR_BUSY);
        } else if (AppBench_Start((app_bench_mode_t)d[0], d[1])) {
            send_ack(BLE_CMD_BENCH_START);
        } else {
            send_nack(BLE_CMD_BENCH_START, BLE_ERR_BAD_PARAM);
        }
        break;
    case BLE_CMD_BENCH_ABORT:
        AppBench_Abort();
        send_ack(BLE_CMD_BENCH_ABORT);
        break;
    case BLE_CMD_BENCH_LINE_STRAIGHT_START:
        if (!pid_schedule_write_allowed() || len < 1) {
            send_nack(BLE_CMD_BENCH_LINE_STRAIGHT_START, BLE_ERR_BUSY);
        } else if (AppBench_Start(APP_BENCH_LINE_STRAIGHT, d[0])) {
            send_ack(BLE_CMD_BENCH_LINE_STRAIGHT_START);
        } else {
            send_nack(BLE_CMD_BENCH_LINE_STRAIGHT_START, BLE_ERR_BAD_PARAM);
        }
        break;
    case BLE_CMD_BENCH_LINE_CURVE_START:
        if (!pid_schedule_write_allowed() || len < 1) {
            send_nack(BLE_CMD_BENCH_LINE_CURVE_START, BLE_ERR_BUSY);
        } else if (AppBench_Start(APP_BENCH_LINE_CURVE, d[0])) {
            send_ack(BLE_CMD_BENCH_LINE_CURVE_START);
        } else {
            send_nack(BLE_CMD_BENCH_LINE_CURVE_START, BLE_ERR_BAD_PARAM);
        }
        break;
    case BLE_CMD_PARAM_LOAD:
        BLE_Param_LoadDefaults();
        send_ack(BLE_CMD_PARAM_LOAD);
        break;
    case BLE_CMD_PARAM_LIST: {
        /* 返回参数列表: 逐个条目单独发送 */
        uint8_t i, buf[32];
        for (i = 0; i < g_param_count; i++) {
            char name[24];
            float val, def, min, max;
            if (BLE_Param_GetEntry(i, name, &val, &def, &min, &max)) {
                uint8_t name_len = (uint8_t)strlen(name);
                uint8_t n = name_len + 17; /* id:1 + name:n + val:4 + def:4 + min:4 + max:4 */
                if (n > 30) n = 30;
                buf[0] = g_param_count; /* 总数 */
                buf[1] = i;             /* 当前索引 */
                buf[2] = name_len;
                memcpy(&buf[3], name, name_len);
                uint8_t off = 3 + name_len;
                write_f32(buf, off,      val); off += 4;
                write_f32(buf, off,      def); off += 4;
                write_f32(buf, off,      min); off += 4;
                write_f32(buf, off,      max);
                BLE_Protocol_Send(BLE_CMD_PARAM_LIST, buf, n);
            }
        }
        break;
    }

    /* ===== 示教编程 ===== */
    case BLE_CMD_TEACH_START:
        if (len >= 1) AppTeach_StartRecording(d[0]);
        send_ack(BLE_CMD_TEACH_START);
        break;
    case BLE_CMD_TEACH_STOP:
        AppTeach_StopRecording();
        g_ble_state = BLE_STATE_IDLE;
        send_ack(BLE_CMD_TEACH_STOP);
        break;
    case BLE_CMD_TEACH_RECORD_WP:
        AppTeach_RecordWaypoint();
        send_ack(BLE_CMD_TEACH_RECORD_WP);
        break;
    case BLE_CMD_TEACH_PLAY:
        if (len >= 2) {
            AppTeach_StartPlayback(d[0]);
            g_ble_state = BLE_STATE_PLAYING;
        }
        send_ack(BLE_CMD_TEACH_PLAY);
        break;
    case BLE_CMD_TEACH_PAUSE:
        AppTeach_Pause();
        send_ack(BLE_CMD_TEACH_PAUSE);
        break;
    case BLE_CMD_TEACH_RESUME:
        AppTeach_Resume();
        send_ack(BLE_CMD_TEACH_RESUME);
        break;
    case BLE_CMD_TEACH_ABORT:
        AppTeach_Abort();
        g_ble_state = BLE_STATE_IDLE;
        send_ack(BLE_CMD_TEACH_ABORT);
        break;
    case BLE_CMD_TEACH_WAYPOINT:
        if (len >= 12) {
            waypoint_t wp;
            wp.encL       = read_i32(d, 0);
            wp.encR       = read_i32(d, 4);
            wp.speed      = d[8];
            wp.action     = d[9];
            wp.timeout_ms = (uint16_t)(d[10] | (d[11] << 8));
            AppTeach_AddWaypoint(&wp);
            send_ack(BLE_CMD_TEACH_WAYPOINT);


        } else {
            send_nack(BLE_CMD_TEACH_WAYPOINT, BLE_ERR_BAD_PARAM);
        }
        break;
    case BLE_CMD_TEACH_CLEAR:
        AppTeach_ClearSequence();
        send_ack(BLE_CMD_TEACH_CLEAR);
        break;
    case BLE_CMD_TEACH_LIST: {
        uint8_t buf[4], count;
        char names[4][16];
        count = AppTeach_ListSequences(names);
        buf[0] = count;
        buf[1] = 0; /* reserved */
        BLE_Protocol_Send(BLE_CMD_TEACH_LIST, buf, 2);
        break;
    }



    /* ===== 遥测 ===== */
    case BLE_CMD_TELEM_START:
        if (len >= 2) {
            telem_rate_hz  = d[0];
            telem_enabled  = true;
            telem_last_ms  = 0;
            send_ack(BLE_CMD_TELEM_START);
        }
        break;
    case BLE_CMD_TELEM_STOP:
        telem_enabled = false;
        BLE_TxDropTelemetry();
        send_ack(BLE_CMD_TELEM_STOP);
        break;
    case BLE_CMD_BALL_TUNE_SET:
        /* [version:u16][kp_x1000:u16][kd_x1000:u16][reserved:u8][xor8:u8].
         * A complete pair prevents transient mixed gains. It is accepted only
         * after the pipe has settled near centre with fresh, trusted vision. */
        if (len != BALL_TUNE_PAYLOAD_LEN || xor8_bytes(d, 7U) != d[7]) {
            send_nack(BLE_CMD_BALL_TUNE_SET, BLE_ERR_BAD_PARAM);
        } else {
            BallPosition_t ball = BallVision_GetPosition();
            uint32_t age_ms = BallVision_TimeSinceLastFrame();
            BallPdMonitor_t pd = BallPdMonitor_Update(&ball, age_ms);
            float kp = (float)((uint16_t)read_i16(d, 2)) / 1000.0f;
            float kd = (float)((uint16_t)read_i16(d, 4)) / 1000.0f;
            int32_t error = pd.error_px;
            if (error < 0) error = -error;
            if (pd.status != BALL_PD_OK || error > BALL_TUNE_SAFE_ERROR_PX ||
                !StepperTest_IsTuningSafe() || !BallPdMonitor_SetGains(kp, kd)) {
                send_nack(BLE_CMD_BALL_TUNE_SET, BLE_ERR_BUSY);
            } else {
                send_ack(BLE_CMD_BALL_TUNE_SET);
            }
        }
        break;
    case BLE_CMD_CAL_CAPTURE:
        if (len < 2U) {
            send_nack(BLE_CMD_CAL_CAPTURE, BLE_ERR_BAD_PARAM);
        } else if (H1CalibrationCaptureStart(d[0], d[1])) {
            send_ack(BLE_CMD_CAL_CAPTURE);
        } else {
            send_nack(BLE_CMD_CAL_CAPTURE, BLE_ERR_BUSY);
        }
        break;
    case BLE_CMD_H1_DEBUG_MODE:
        if (len < 1U) {
            send_nack(BLE_CMD_H1_DEBUG_MODE, BLE_ERR_BAD_PARAM);
        } else {
            g_h1_app_debug_mode = d[0] != 0U;
            /* OLED 停刷后，调试遥测中的姿态数据必须来自持续更新的IMU。 */
            if (g_h1_app_debug_mode) {
                g_imu_enabled = true;
            } else if ((Competition_GetState() == COMP_STATE_RUNNING) &&
                       (Competition_GetBallMode() != COMP_BALL_MODE_MOTION_BALANCE)) {
                /* T3/T4 运动滚球需要实时 yawRate 前馈；不能因退出App调试模式
                 * 把IMU更新关掉。普通比赛任务仍可按原策略关闭IMU避免I2C占用。 */
                g_imu_enabled = false;
            } else {
                g_imu_enabled = true;
            }
            send_ack(BLE_CMD_H1_DEBUG_MODE);
        }
        break;

    /* ===== 调试消息 ===== */
    case BLE_CMD_GET_DEBUG:
        BLE_DebugFlush();
        send_ack(BLE_CMD_GET_DEBUG);
        break;
    case BLE_CMD_CLEAR_DEBUG:
        BLE_DebugClear();
        send_ack(BLE_CMD_CLEAR_DEBUG);
        break;

    /* ===== 指令示教 ===== */
    case BLE_CMD_INS_CLEAR:
        AppIns_Clear();
        send_ack(BLE_CMD_INS_CLEAR);
        break;
    case BLE_CMD_INS_APPEND:
        if (len >= 9) {
            ins_t ins;
            memset(&ins, 0, sizeof(ins));
            ins.opcode = d[0];
            ins.p1 = read_i16(d, 1);
            ins.p2 = read_i16(d, 3);
            ins.p3 = read_i16(d, 5);
            ins.p4 = read_i16(d, 7);
            if (AppIns_Append(&ins)) send_ack(BLE_CMD_INS_APPEND);
            else send_nack(BLE_CMD_INS_APPEND, BLE_ERR_BUSY);
        }
        break;
    case BLE_CMD_INS_INSERT:
        if (len >= 10) {
            ins_t ins;
            memset(&ins, 0, sizeof(ins));
            ins.opcode = d[1];
            ins.p1 = read_i16(d, 2);
            ins.p2 = read_i16(d, 4);
            ins.p3 = read_i16(d, 6);
            ins.p4 = read_i16(d, 8);
            if (AppIns_Insert(d[0], &ins)) send_ack(BLE_CMD_INS_INSERT);
            else send_nack(BLE_CMD_INS_INSERT, BLE_ERR_BAD_PARAM);
        }
        break;
    case BLE_CMD_INS_DELETE:
        if (len >= 1 && AppIns_Delete(d[0]))
            send_ack(BLE_CMD_INS_DELETE);
        else send_nack(BLE_CMD_INS_DELETE, BLE_ERR_BAD_PARAM);
        break;
    case BLE_CMD_INS_EXEC:
        AppIns_Exec();
        send_ack(BLE_CMD_INS_EXEC);
        break;
    case BLE_CMD_INS_STOP:
        AppIns_Stop();
        send_ack(BLE_CMD_INS_STOP);
        break;
    case BLE_CMD_INS_PAUSE:
        AppIns_Pause();
        send_ack(BLE_CMD_INS_PAUSE);
        break;
    case BLE_CMD_INS_RESUME:
        AppIns_Resume();
        send_ack(BLE_CMD_INS_RESUME);
        break;
    case BLE_CMD_INS_STEP:
        AppIns_Step();
        send_ack(BLE_CMD_INS_STEP);
        break;
    case BLE_CMD_INS_GET_PC: {
        uint8_t pc[2];
        pc[0] = (uint8_t)(AppIns_GetIP() & 0xFF);
        pc[1] = (uint8_t)((AppIns_GetIP() >> 8) & 0xFF);
        BLE_Protocol_Send(BLE_CMD_INS_GET_PC, pc, 2);
        break;
    }
    case BLE_CMD_INS_GET_BUF: {
        /* 分段发送完整指令缓冲区。
         * 包格式: [total_count:u8][total_chunks:u8][chunk_index:u8][this_count:u8][指令数据...]
         * 每条指令 9 字节, 每包最多 27 条 (4 + 27*9 = 247 ≤ 250)。
         * App 收齐 total_chunks 个包后重建完整脚本。 */
        uint8_t total = (uint8_t)AppIns_GetCount();
        if (total == 0) {
            uint8_t resp[4] = { 0, 0, 0, 0 };
            BLE_Protocol_Send(BLE_CMD_INS_GET_BUF, resp, 4);
            break;
        }
        const uint8_t PER_CHUNK = 27U;
        uint8_t total_chunks = (uint8_t)((total + PER_CHUNK - 1) / PER_CHUNK);
        uint8_t chunk_idx;
        for (chunk_idx = 0; chunk_idx < total_chunks; chunk_idx++) {
            uint8_t buf[247];
            uint8_t this_count = (chunk_idx == total_chunks - 1)
                               ? (uint8_t)(total - chunk_idx * PER_CHUNK)
                               : PER_CHUNK;
            buf[0] = total;
            buf[1] = total_chunks;
            buf[2] = chunk_idx;
            buf[3] = this_count;
            uint8_t off = 4;
            uint8_t i;
            for (i = 0; i < this_count; i++) {
                ins_t ins;
                if (AppIns_GetAt((uint8_t)(chunk_idx * PER_CHUNK + i), &ins)) {
                    buf[off]     = ins.opcode;
                    write_i16(buf, off + 1, ins.p1);
                    write_i16(buf, off + 3, ins.p2);
                    write_i16(buf, off + 5, ins.p3);
                    write_i16(buf, off + 7, ins.p4);
                } else {
                    memset(&buf[off], 0, 9);
                }
                off += 9;
            }
            BLE_Protocol_Send(BLE_CMD_INS_GET_BUF, buf, off);
        }
        break;
    }

    default:
        send_nack(pkt->cmd, BLE_ERR_UNSUPPORTED);
        break;
    }

    BLE_Protocol_Done();
}

/* ==================== 遥测帧发送 ==================== */
void AppBLE_TelemTrySend(uint32_t now_ms)
{
    if (!telem_enabled) return;

    uint32_t interval = 1000 / telem_rate_hz;
    if (now_ms - telem_last_ms < interval) return;
    telem_last_ms = now_ms;

    /* 旧App兼容前45字节；H题页面读取循迹快照与本圈结果扩展。 */
    uint8_t buf[67];
    float line_error = 0.0f, line_steer = 0.0f, line_base_speed = 0.0f;
    uint8_t line_debug_gray = 0U, line_debug_valid = 0U;
    uint8_t h1_result = 0U, h1_max_black_bits = 0U;
    uint32_t h1_elapsed_ms = 0U;
    int32_t h1_left_pulses = 0, h1_right_pulses = 0;

    /* t_ms: u32 */
    write_u32(buf, 0, now_ms);

    /* encL, encR: i32 */
    write_i32(buf, 4,  encoderLeft.total_count);
    write_i32(buf, 8,  encoderRight.total_count);

    /* gray: u8 (8路数字量)。H题运行时使用外环同周期快照，避免遥测发送时又采样一次而与误差错位。 */
    extern uint8_t Get_Gray_Digital(void);
    buf[12] = Get_Gray_Digital();

    /* yaw: i16 (度 × 10) */
    write_i16(buf, 13, (int16_t)(g_imuData.yaw * 10.0f));

    /* speedL, speedR: i16 (cm/s × 10) */
    float spdL = Encoder_GetSpeedLine(&encoderLeft);
    float spdR = Encoder_GetSpeedLine(&encoderRight);
    write_i16(buf, 15, (int16_t)(spdL * 10.0f));
    write_i16(buf, 17, (int16_t)(spdR * 10.0f));

    /* IMU 扩展：角速度为 °/s ×100，姿态为 °×10。
     * App 保持兼容旧版前 19 字节遥测帧。 */
    write_i16(buf, 19, (int16_t)(g_imuData.gx * 57.2957795f * 100.0f));
    write_i16(buf, 21, (int16_t)(g_imuData.gy * 57.2957795f * 100.0f));
    write_i16(buf, 23, (int16_t)(g_imuData.yawRate * 100.0f));
    write_i16(buf, 25, (int16_t)(g_imuData.roll * 10.0f));

    /* 当前 Z 轴原始零偏 LSB，供 App 展示与静止闭环校准。 */
    write_i32(buf, 27, ImuApp_GetGyroBiasZ());

    /* PID 调参扩展：旧 App 继续兼容前31字节。 */
    buf[31] = g_ble_state;
    write_i16(buf, 32, (int16_t)(targetSpeedLeft * 10.0f));
    write_i16(buf, 34, (int16_t)(targetSpeedRight * 10.0f));
    write_i16(buf, 36, (int16_t)(pidLeftSpeed.error * 10.0f));
    write_i16(buf, 38, (int16_t)(pidRightSpeed.error * 10.0f));
    write_i16(buf, 40, (int16_t)(pidLeftSpeed.output));
    write_i16(buf, 42, (int16_t)(pidRightSpeed.output));
    buf[44] = Get_Gray_Digital() == 0U ? 1U : 0U; /* 1=当前未检测到黑线 */

    /* H题循迹外环扩展：误差/转向/当前段速度均按×10缩放，最后给出有效标志。
     * 仅由比赛段表写入，不将App调试动作引入本地控制回路。 */
    Competition_GetLineDebugSnapshot(&line_error, &line_steer, &line_base_speed,
                                     &line_debug_gray, &line_debug_valid);
    if (line_debug_valid) buf[12] = line_debug_gray;
    write_i16(buf, 45, (int16_t)(line_error * 10.0f));
    write_i16(buf, 47, (int16_t)(line_steer * 10.0f));
    write_i16(buf, 49, (int16_t)(line_base_speed * 10.0f));
    buf[51] = line_debug_valid;
    buf[52] = g_h1_app_debug_mode ? 1U : 0U;

    /* H题本圈结果扩展：结束后保持冻结值，运行中实时更新行程和用时。 */
    Competition_GetH1ResultSnapshot(&h1_result, &h1_elapsed_ms,
                                    &h1_left_pulses, &h1_right_pulses,
                                    &h1_max_black_bits);
    buf[53] = h1_result;
    write_u32(buf, 54, h1_elapsed_ms);
    write_i32(buf, 58, h1_left_pulses);
    write_i32(buf, 62, h1_right_pulses);
    buf[66] = h1_max_black_bits;

    BLE_Protocol_Send(BLE_CMD_TELEM_FRAME, buf, 67);

    /* BALL_TELEM payload (38 B, little-endian):
     * t_ms:u32, x/y/vx/vy:i16, confidence:u8, valid:u8, pd_status:u8,
     * step_state:u8, error/req/tgt:i16, pos:i32, age:u16, seq:u32,
     * rx_errors:u16, kp_x1000:i16, kd_x1000:i16. */
    if (now_ms - ball_telem_last_ms >= (1000U / BALL_TELEM_RATE_HZ)) {
        uint8_t ball_buf[38] = {0};
        BallPosition_t ball = BallVision_GetPosition();
        uint32_t age_ms = BallVision_TimeSinceLastFrame();
        BallPdMonitor_t pd = BallPdMonitor_Update(&ball, age_ms);
        float kp = 0.0f, kd = 0.0f;
        uint32_t rx_errors = BallVision_GetRxErrorCount();
        BallPdMonitor_GetGains(&kp, &kd);
        ball_telem_last_ms = now_ms;
        /* The original generic frame is sent first for compatibility. If it
         * filled the telemetry queue, defer this lower-priority duplicate
         * sample rather than affecting ACK or emergency-stop delivery. */
        write_u32(ball_buf, 0, now_ms);
        write_i16(ball_buf, 4, ball.x); write_i16(ball_buf, 6, ball.y);
        write_i16(ball_buf, 8, ball.vx); write_i16(ball_buf, 10, ball.vy);
        ball_buf[12] = ball.confidence; ball_buf[13] = ball.valid ? 1U : 0U;
        ball_buf[14] = (uint8_t)pd.status;
        ball_buf[15] = (uint8_t)StepperTest_GetState();
        write_i16(ball_buf, 16, pd.error_px);
        write_i16(ball_buf, 18, pd.requested_steps);
        write_i16(ball_buf, 20, StepperTest_GetTargetSteps());
        write_i32(ball_buf, 22, tmc2209_get_position_steps());
        write_i16(ball_buf, 26, (int16_t)(uint16_t)(age_ms > 65535U ? 65535U : age_ms));
        write_u32(ball_buf, 28, ball.seq);
        write_i16(ball_buf, 32, rx_errors > 32767U ? 32767 : (int16_t)rx_errors);
        write_i16(ball_buf, 34, (int16_t)(kp * 1000.0f));
        write_i16(ball_buf, 36, (int16_t)(kd * 1000.0f));
        BLE_Protocol_Send(BLE_CMD_BALL_TELEM, ball_buf, sizeof(ball_buf));
    }
}
