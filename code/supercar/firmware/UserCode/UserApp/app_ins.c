/**
 * @file    app_ins.c
 * @brief   指令示教脚本解释器 — 实现
 *
 * 支持 40+ opcode, 含控制流 (if/for/while/label/jump).
 * 运动类指令在多周期内完成 (每 10ms 检查是否到位).
 */

#include "app_ins.h"
#include "ble_protocol.h"
#include "ble_param.h"
#include "encoder_driver.h"
#include "app_imu.h"
#include "app_ble.h"
#include "ble.h"
#include "pid.h"
#include "No_Mcu_Ganv_Grayscale_Sensor.h"
#include "sys_time.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ==================== 指令缓冲区 ==================== */
static ins_t     ins_buf[INS_BUF_MAX];
static uint16_t  ins_count;
static uint16_t  ins_ip;       /* Instruction Pointer */

/* ==================== 标签表 ==================== */
static uint16_t  label_table[LABEL_MAX];
static bool      label_valid[LABEL_MAX];

/* ==================== 循环嵌套栈 ==================== */
typedef struct {
    uint8_t  type;    /* 0=FOR, 1=WHILE */
    uint16_t loop_ip;
    int16_t  start_val;
    int16_t  end_val;
    int16_t  step;
    uint8_t  var_id;
} nest_t;

static nest_t    nest_stack[NEST_MAX];
static uint8_t   nest_sp;

/* ==================== 循环变量 ==================== */
static int16_t   loop_vars[LOOP_VAR_COUNT];

/* ==================== 执行状态 ==================== */
static bool      ins_running;
static bool      ins_paused;
static ins_exec_state_t exec_state;

/* 运动类指令的执行上下文 */
static struct {
    int32_t  start_encL;
    int32_t  start_encR;
    float    start_yaw;
    uint32_t start_ms;
    uint32_t timeout_ms;
    int32_t  last_dist;      /* 上一帧距离, 编码器卡死检测 */
    uint16_t stuck_frames;   /* 距离不增长帧数 */
} ins_ctx;

/* 外部变量 */
extern Encoder_t encoderLeft;
extern Encoder_t encoderRight;
extern float     g_ble_target_left;
extern float     g_ble_target_right;
extern uint8_t   Get_Gray_Digital(void);

/* 全局 PID (empty.c 定义, ISR + 外环共用) */
extern PID_t         pidLeftSpeed;
extern PID_t         pidRightSpeed;
extern PID_t         pidLine;
extern PID_t         pidAngle;
extern PID_t         pidPosition;
extern No_MCU_Sensor g_GraySensor;

/* 编码器脉冲换算: 1456 pulse/rev, 轮周长 20.42cm → 71.3 pulse/cm */
#define INS_PULSES_PER_CM  71.3f

/* ==================== PID 闭环辅助 ==================== */

static float ins_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float ins_relative_yaw(float current, float start)
{
    float v = current - start;
    while (v >  180.0f) v -= 360.0f;
    while (v < -180.0f) v += 360.0f;
    return v;
}

/* 双轮平均距离 (脉冲), 与 app_competition.comp_seg_dist() 一致 */
static int32_t ins_seg_dist(void)
{
    int32_t dl = encoderLeft.total_count  - ins_ctx.start_encL;
    int32_t dr = encoderRight.total_count - ins_ctx.start_encR;
    if (dl < 0) dl = -dl;
    if (dr < 0) dr = -dr;
    return (dl + dr) / 2;
}

/* 运动指令错误码 (通过 BLE_CMD_INS_EVENT 发送) */
#define INS_ERR_TIMEOUT     0xFF
#define INS_ERR_ENCODER     0xFE
#define INS_ERR_IMU         0xFD

/* 前置声明 (do_stop 定义在后面, ins_motion_error 先用到) */
static void do_stop(void);

/* 运动指令出错: 停车 + 设 ERROR + 停整个脚本 + 通知 App */
static void ins_motion_error(uint8_t err_code)
{
    do_stop();
    exec_state  = INS_EXEC_ERROR;
    ins_running = false;
    g_ble_state = BLE_STATE_IDLE;
    BLE_Protocol_Send(BLE_CMD_INS_EVENT, &err_code, 1);
}

/* 运动指令每帧安全检查: 超时 + 编码器卡死。
 * 返回 true=检查通过, false=已出错(调用方应 break)。
 * timeout_ms=0 表示不检查超时。 */
static bool ins_motion_check(uint32_t timeout_ms, int32_t cur_dist)
{
    /* 超时检查 */
    if (timeout_ms > 0 &&
        mspm0_get_clock_ms() - ins_ctx.start_ms >= timeout_ms) {
        ins_motion_error(INS_ERR_TIMEOUT);
        return false;
    }
    /* 编码器卡死检测: 1 秒(100 帧)距离不增长视为卡死 */
    if (cur_dist <= ins_ctx.last_dist + 1) {
        if (++ins_ctx.stuck_frames > 100) {
            ins_motion_error(INS_ERR_ENCODER);
            return false;
        }
    } else {
        ins_ctx.stuck_frames = 0;
    }
    ins_ctx.last_dist = cur_dist;
    return true;
}

/* 根据距离(cm*10)和速度(cm/s)估算超时, 3 倍裕度, 限制 3~30 秒 */
static uint32_t ins_est_timeout(int16_t dist_cm10, float speed_cm_s)
{
    if (speed_cm_s < 1.0f) speed_cm_s = 1.0f;
    float est_s = ((float)dist_cm10 / 10.0f) / speed_cm_s;
    uint32_t ms = (uint32_t)(est_s * 3000.0f);
    if (ms < 3000U)  ms = 3000U;
    if (ms > 30000U) ms = 30000U;
    return ms;
}

/* 运动指令进入时加载该速度对应的 schedule 插值参数到全局 PID */
static pid_schedule_node_t ins_node;

static void ins_load_schedule(float speed_cm_s)
{
    BLE_Param_InterpolatePidSchedule(speed_cm_s, &ins_node);
    pidLine.kp            = ins_node.line_kp;
    pidLine.kd            = ins_node.line_kd;
    pidLine.output_limit_p =  ins_node.line_max_steer;
    pidLine.output_limit_n = -ins_node.line_max_steer;
    pidAngle.kp            = ins_node.yaw_kp;
    pidAngle.kd            = ins_node.yaw_kd;
    pidAngle.output_limit_p =  ins_node.yaw_max_steer;
    pidAngle.output_limit_n = -ins_node.yaw_max_steer;
    pidPosition.kp         = ins_node.pos_kp;
}

/* 位置闭环减速: 返回当前应使用的巡航速度 */
static float ins_cruise_speed(float cruise, int32_t dist_target, int32_t dist_done)
{
    int32_t remaining = dist_target - dist_done;
    if (remaining <= 0) return ins_node.end_speed;

    PID_SetTarget(&pidPosition, 0.0f);
    float pos_speed = PID_Calc(&pidPosition, (float)(-remaining));
    if (pos_speed > cruise)     pos_speed = cruise;
    if (pos_speed < ins_node.end_speed) pos_speed = ins_node.end_speed;

    /* 梯形减速剖面 */
    int32_t decel = (int32_t)ins_node.decel_pulses;
    if (decel > 200 && remaining < decel) {
        float ratio = (float)remaining / (float)decel;
        float profile = ins_node.end_speed + (cruise - ins_node.end_speed) * ratio;
        if (profile < pos_speed) pos_speed = profile;
    }
    return pos_speed;
}

/* ==================== 辅助函数 ==================== */
static uint16_t find_label(uint8_t id)
{
    if (id < LABEL_MAX && label_valid[id])
        return label_table[id];
    return 0xFFFF;
}

static void do_stop(void)
{
    g_ble_target_left  = 0;
    g_ble_target_right = 0;
}

static bool eval_cond(uint8_t ctype, int16_t cval)
{
    switch (ctype) {
    case 0x00: return true;  /* ALWAYS */
    case 0x01: return Get_Gray_Digital() != 0;  /* ON_BLACK */
    case 0x02: return Get_Gray_Digital() == 0;  /* NO_BLACK */
    case 0x03: { /* ENC_GT */
        int32_t cur = (encoderLeft.total_count - ins_ctx.start_encL) / 71;
        return cur > cval;
    }
    case 0x04: return (g_imuData.yaw - ins_ctx.start_yaw) > (float)cval * 0.1f;
    case 0x05: return (g_imuData.yaw - ins_ctx.start_yaw) < (float)cval * 0.1f;
    case 0x09: return g_imuData.hw_ok;  /* IMU_OK */
    case 0x0A: return !g_imuData.hw_ok; /* IMU_FAIL */
    default:   return false;
    }
}

static uint16_t skip_to_endif(uint16_t ip)
{
    uint8_t depth = 1;
    while (ip < ins_count) {
        if (ins_buf[ip].opcode == 0x63) depth++;       /* IF */
        else if (ins_buf[ip].opcode == 0x65) {          /* END_IF */
            depth--;
            if (depth == 0) return ip;
        }
        ip++;
    }
    return ins_count;
}

static uint16_t skip_to_else(uint16_t ip)
{
    uint8_t depth = 1;
    while (ip < ins_count) {
        if (ins_buf[ip].opcode == 0x63) depth++;
        else if (ins_buf[ip].opcode == 0x64 && depth == 1) return ip; /* ELSE */
        else if (ins_buf[ip].opcode == 0x65) depth--;
        ip++;
    }
    return skip_to_endif(ip);
}

static uint16_t skip_to_endwhile(uint16_t ip)
{
    uint8_t depth = 1;
    while (ip < ins_count) {
        if (ins_buf[ip].opcode == 0x68) depth++;        /* WHILE */
        else if (ins_buf[ip].opcode == 0x69) {           /* END_WHILE */
            depth--;
            if (depth == 0) return ip;
        }
        ip++;
    }
    return ins_count;
}

static uint16_t find_endfor(uint16_t ip)
{
    uint8_t depth = 1;
    while (ip < ins_count) {
        if (ins_buf[ip].opcode == 0x66) depth++;        /* FOR */
        else if (ins_buf[ip].opcode == 0x67) {           /* END_FOR */
            depth--;
            if (depth == 0) return ip;
        }
        ip++;
    }
    return ins_count;
}

/* ==================== Init ==================== */
void AppIns_Init(void)
{
    memset(ins_buf, 0, sizeof(ins_buf));
    ins_count   = 0;
    ins_ip      = 0;
    ins_running = false;
    ins_paused  = false;
    exec_state  = INS_EXEC_IDLE;
    nest_sp     = 0;
    memset(label_valid, 0, sizeof(label_valid));
    memset(loop_vars, 0, sizeof(loop_vars));
    do_stop();
}

void AppIns_Clear(void)
{
    AppIns_Init();
}

bool AppIns_Append(const ins_t *ins)
{
    if (ins_count >= INS_BUF_MAX) return false;
    memcpy(&ins_buf[ins_count++], ins, sizeof(ins_t));
    return true;
}

bool AppIns_Insert(uint8_t index, const ins_t *ins)
{
    if (ins_count >= INS_BUF_MAX || index > ins_count) return false;
    uint16_t i;
    for (i = ins_count; i > index; i--)
        memcpy(&ins_buf[i], &ins_buf[i-1], sizeof(ins_t));
    memcpy(&ins_buf[index], ins, sizeof(ins_t));
    ins_count++;
    /* 更新受影响的标签 */
    for (i = 0; i < LABEL_MAX; i++)
        if (label_valid[i] && label_table[i] >= index)
            label_table[i]++;
    return true;
}

bool AppIns_Delete(uint8_t index)
{
    if (index >= ins_count) return false;
    uint16_t i;
    for (i = index; i < ins_count - 1; i++)
        memcpy(&ins_buf[i], &ins_buf[i+1], sizeof(ins_t));
    ins_count--;
    for (i = 0; i < LABEL_MAX; i++)
        if (label_valid[i] && label_table[i] > index)
            label_table[i]--;
    return true;
}

uint16_t AppIns_GetCount(void) { return ins_count; }
uint16_t AppIns_GetIP(void)    { return ins_ip; }

bool AppIns_GetAt(uint8_t index, ins_t *out)
{
    if (index >= ins_count || out == NULL) return false;
    memcpy(out, &ins_buf[index], sizeof(ins_t));
    return true;
}

bool AppIns_IsActive(void)
{
    return ins_running;
}

void AppIns_Exec(void)
{
    /* 清除 BLE 遥控残留 ramp，防止旧遥控值干扰脚本执行 */
    BLE_ResetDrive();
    ins_ip     = 0;
    ins_running = true;
    ins_paused  = false;
    exec_state = INS_EXEC_RUNNING;
    nest_sp    = 0;
    /* 扫描一遍建立标签表 */
    uint16_t i;
    memset(label_valid, 0, sizeof(label_valid));
    for (i = 0; i < ins_count; i++) {
        if (ins_buf[i].opcode == 0x60) { /* LABEL */
            uint8_t id = (uint8_t)ins_buf[i].p1;
            if (id < LABEL_MAX) {
                label_table[id] = i;
                label_valid[id] = true;
            }
        }
    }
    g_ble_state = BLE_STATE_INS_EXEC;
}

void AppIns_Stop(void)
{
    ins_running = false;
    ins_paused  = false;
    exec_state  = INS_EXEC_IDLE;
    do_stop();
    g_ble_state = BLE_STATE_IDLE;
}

void AppIns_Pause(void)
{
    ins_paused = true;
    g_ble_state = BLE_STATE_INS_PAUSED;
}

void AppIns_Resume(void)
{
    ins_paused = false;
    g_ble_state = BLE_STATE_INS_EXEC;
}

void AppIns_Step(void)
{
    /* 单步: 暂停后执行一条 */
    ins_paused = false;
    /* will execute one instruction then pause again */
}

/* ==================== 主任务 ==================== */
void AppIns_Task(void)
{
    if (!ins_running || ins_paused) return;
    if (ins_count == 0) return;

    bool advance;  /* 本条指令是否执行完毕, 可以跳下一条 */

    while (ins_ip < ins_count) {
        ins_t *ins = &ins_buf[ins_ip];
        advance = false;

        switch (ins->opcode) {

        /* ===== 直行类 ===== */
        case 0x01: { /* MOVE_CM: p1=dist_cm*10, p2=speed — 位置闭环直行 */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_encL = encoderLeft.total_count;
                ins_ctx.start_encR = encoderRight.total_count;
                ins_ctx.start_yaw  = g_imuData.yaw;
                ins_ctx.start_ms   = mspm0_get_clock_ms();
                ins_ctx.timeout_ms = ins_est_timeout(ins->p1, (float)ins->p2);
                ins_ctx.last_dist  = 0;
                ins_ctx.stuck_frames = 0;
                ins_load_schedule((float)ins->p2);
                PID_Reset(&pidPosition);
                PID_Reset(&pidAngle);
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            int32_t dist_target = (int32_t)((float)ins->p1 * INS_PULSES_PER_CM / 10.0f);
            int32_t cur = ins_seg_dist();
            if (!ins_motion_check(ins_ctx.timeout_ms, cur)) break;
            float speed = ins_cruise_speed((float)ins->p2, dist_target, cur);
            /* 航向保持 (若有 IMU) */
            float steer = 0.0f;
            if (g_imuData.hw_ok) {
                float yaw_err = ins_relative_yaw(g_imuData.yaw, ins_ctx.start_yaw);
                PID_SetTarget(&pidAngle, 0.0f);
                steer = PID_Calc(&pidAngle, yaw_err);
                steer = ins_clampf(steer, -speed * 0.5f, speed * 0.5f);
            }
            g_ble_target_left  = speed - steer;
            g_ble_target_right = speed + steer;
            if (cur >= dist_target) {
                do_stop();
                exec_state = INS_EXEC_IDLE;
                advance = true;
            }
            break;
        }

        case 0x02: { /* MOVE_RAMP: p1=dist_cm*10 — 位置闭环, 用 base_speed */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_encL = encoderLeft.total_count;
                ins_ctx.start_encR = encoderRight.total_count;
                ins_ctx.start_yaw  = g_imuData.yaw;
                ins_ctx.start_ms   = mspm0_get_clock_ms();
                float base = BLE_Param_GetBaseSpeed();
                ins_ctx.timeout_ms = ins_est_timeout(ins->p1, base);
                ins_ctx.last_dist  = 0;
                ins_ctx.stuck_frames = 0;
                ins_load_schedule(base);
                PID_Reset(&pidPosition);
                PID_Reset(&pidAngle);
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            int32_t dist_target = (int32_t)((float)ins->p1 * INS_PULSES_PER_CM / 10.0f);
            int32_t cur = ins_seg_dist();
            if (!ins_motion_check(ins_ctx.timeout_ms, cur)) break;
            float base = BLE_Param_GetBaseSpeed();
            float speed = ins_cruise_speed(base, dist_target, cur);
            float steer = 0.0f;
            if (g_imuData.hw_ok) {
                float yaw_err = ins_relative_yaw(g_imuData.yaw, ins_ctx.start_yaw);
                PID_SetTarget(&pidAngle, 0.0f);
                steer = PID_Calc(&pidAngle, yaw_err);
                steer = ins_clampf(steer, -speed * 0.5f, speed * 0.5f);
            }
            g_ble_target_left  = speed - steer;
            g_ble_target_right = speed + steer;
            if (cur >= dist_target) {
                do_stop();
                exec_state = INS_EXEC_IDLE;
                advance = true;
            }
            break;
        }

        case 0x03: { /* MOVE_TRANSITION: p1=dist_cm*10 — 位置闭环, 到位不停 */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_encL = encoderLeft.total_count;
                ins_ctx.start_encR = encoderRight.total_count;
                ins_ctx.start_yaw  = g_imuData.yaw;
                ins_ctx.start_ms   = mspm0_get_clock_ms();
                float base = BLE_Param_GetBaseSpeed();
                ins_ctx.timeout_ms = ins_est_timeout(ins->p1, base);
                ins_ctx.last_dist  = 0;
                ins_ctx.stuck_frames = 0;
                ins_load_schedule(base);
                PID_Reset(&pidPosition);
                PID_Reset(&pidAngle);
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            int32_t dist_target = (int32_t)((float)ins->p1 * INS_PULSES_PER_CM / 10.0f);
            int32_t cur = ins_seg_dist();
            if (!ins_motion_check(ins_ctx.timeout_ms, cur)) break;
            float base = BLE_Param_GetBaseSpeed();
            float speed = ins_cruise_speed(base, dist_target, cur);
            float steer = 0.0f;
            if (g_imuData.hw_ok) {
                float yaw_err = ins_relative_yaw(g_imuData.yaw, ins_ctx.start_yaw);
                PID_SetTarget(&pidAngle, 0.0f);
                steer = PID_Calc(&pidAngle, yaw_err);
                steer = ins_clampf(steer, -speed * 0.5f, speed * 0.5f);
            }
            g_ble_target_left  = speed - steer;
            g_ble_target_right = speed + steer;
            if (cur >= dist_target) {
                exec_state = INS_EXEC_IDLE;
                advance = true;
            }
            break;
        }

        case 0x04: { /* MOVE_IMU: p1=dist_cm*10, p2=speed — 航向保持+位置闭环 */
            if (exec_state == INS_EXEC_IDLE) {
                if (!g_imuData.hw_ok) { ins_motion_error(INS_ERR_IMU); break; }
                ins_ctx.start_encL = encoderLeft.total_count;
                ins_ctx.start_encR = encoderRight.total_count;
                ins_ctx.start_yaw  = g_imuData.yaw;
                ins_ctx.start_ms   = mspm0_get_clock_ms();
                ins_ctx.timeout_ms = ins_est_timeout(ins->p1, (float)ins->p2);
                ins_ctx.last_dist  = 0;
                ins_ctx.stuck_frames = 0;
                ins_load_schedule((float)ins->p2);
                PID_Reset(&pidPosition);
                PID_Reset(&pidAngle);
                PID_SetTarget(&pidAngle, 0.0f);
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            int32_t dist_target = (int32_t)((float)ins->p1 * INS_PULSES_PER_CM / 10.0f);
            int32_t cur = ins_seg_dist();
            if (!ins_motion_check(ins_ctx.timeout_ms, cur)) break;
            float speed = ins_cruise_speed((float)ins->p2, dist_target, cur);
            float yaw_err = ins_relative_yaw(g_imuData.yaw, ins_ctx.start_yaw);
            float steer = PID_Calc(&pidAngle, yaw_err);
            steer = ins_clampf(steer, -speed * 0.5f, speed * 0.5f);
            g_ble_target_left  = speed - steer;
            g_ble_target_right = speed + steer;
            if (cur >= dist_target) {
                do_stop();
                exec_state = INS_EXEC_IDLE;
                advance = true;
            }
            break;
        }

        case 0x05: { /* MOVE_TIME */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_ms = mspm0_get_clock_ms();
                float spdL = (float)ins->p2 * 0.1f;
                float spdR = (float)ins->p3 * 0.1f;
                g_ble_target_left  = spdL;
                g_ble_target_right = spdR;
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            if (mspm0_get_clock_ms() - ins_ctx.start_ms >= (uint32_t)ins->p1) {
                do_stop();
                exec_state = INS_EXEC_IDLE;
                advance = true;
            }
            break;
        }

        /* ===== 旋转类 ===== */
        case 0x10: { /* ROTATE_INPLACE_IMU: p1=delta_deg*10, p2=speed — 航向闭环原地转 */
            if (exec_state == INS_EXEC_IDLE) {
                if (!g_imuData.hw_ok) { ins_motion_error(INS_ERR_IMU); break; }
                ins_ctx.start_yaw = g_imuData.yaw;
                ins_ctx.start_ms  = mspm0_get_clock_ms();
                /* 旋转超时: 角度/角速度(估 30°/s) * 3 倍裕度, 限制 3~15 秒 */
                float est_s = fabsf((float)ins->p1 * 0.1f) / 30.0f;
                ins_ctx.timeout_ms = (uint32_t)(est_s * 3000.0f);
                if (ins_ctx.timeout_ms < 3000U)  ins_ctx.timeout_ms = 3000U;
                if (ins_ctx.timeout_ms > 15000U) ins_ctx.timeout_ms = 15000U;
                ins_ctx.last_dist  = 0;
                ins_ctx.stuck_frames = 0;
                float spd = (float)ins->p2;
                ins_load_schedule(spd);
                PID_Reset(&pidAngle);
                float target_yaw = ins_ctx.start_yaw + (float)ins->p1 * 0.1f;
                PID_SetTarget(&pidAngle, target_yaw);
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            float target_delta = (float)ins->p1 * 0.1f;
            float actual_delta = ins_relative_yaw(g_imuData.yaw, ins_ctx.start_yaw);
            /* 旋转卡死检测: 用角度变化(放大 10 倍映射到 last_dist) */
            int32_t motion_proxy = (int32_t)(fabsf(actual_delta) * 100.0f);
            if (!ins_motion_check(ins_ctx.timeout_ms, motion_proxy)) break;
            /* 航向 PID 输出转向量, 原地转: 左=-steer, 右=+steer */
            float steer = PID_Calc(&pidAngle, g_imuData.yaw);
            float spd = (float)ins->p2;
            /* 接近目标时降速, 防过冲 */
            float remaining = fabsf(target_delta - actual_delta);
            float turn_speed = ins_clampf(steer, -spd, spd);
            if (remaining < 5.0f && fabsf(turn_speed) > spd * 0.4f)
                turn_speed = (turn_speed > 0 ? spd * 0.4f : -spd * 0.4f);
            g_ble_target_left  = -turn_speed;
            g_ble_target_right =  turn_speed;
            if ((target_delta > 0 && actual_delta >= target_delta) ||
                (target_delta < 0 && actual_delta <= target_delta)) {
                do_stop();
                exec_state = INS_EXEC_IDLE;
                advance = true;
            }
            break;
        }

        case 0x11: { /* ROTATE_INPLACE_ENC: p1=delta_deg*10, p2=speed */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_encL = encoderLeft.total_count;
                ins_ctx.start_encR = encoderRight.total_count;
                float spd = (float)ins->p2;
                if (ins->p1 > 0) {
                    g_ble_target_left  = -spd;
                    g_ble_target_right = spd;
                } else {
                    g_ble_target_left  = spd;
                    g_ble_target_right = -spd;
                }
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            /* 脉冲差 = 角度 * (W*PI/360) * 71.3 pulses/cm ≈ angle * 18.7 */
            int32_t target_pulses = abs(ins->p1) * 187 / 10;
            int32_t curL = abs(encoderLeft.total_count - ins_ctx.start_encL);
            int32_t curR = abs(encoderRight.total_count - ins_ctx.start_encR);
            if (curL + curR >= target_pulses) {
                do_stop();
                exec_state = INS_EXEC_IDLE;
                advance = true;
            }
            break;
        }

        /* ===== 循线类 ===== */
        case 0x20: { /* LINE_FOLLOW: p1=dist_cm*10, p2=speed — 循线PID+位置闭环 */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_encL = encoderLeft.total_count;
                ins_ctx.start_encR = encoderRight.total_count;
                ins_ctx.start_ms   = mspm0_get_clock_ms();
                ins_ctx.timeout_ms = ins_est_timeout(ins->p1, (float)ins->p2);
                ins_ctx.last_dist  = 0;
                ins_ctx.stuck_frames = 0;
                ins_load_schedule((float)ins->p2);
                PID_Reset(&pidLine);
                PID_Reset(&pidPosition);
                PID_SetTarget(&pidLine, 0.0f);
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            int32_t dist_target = (int32_t)((float)ins->p1 * INS_PULSES_PER_CM / 10.0f);
            int32_t cur = ins_seg_dist();
            if (!ins_motion_check(ins_ctx.timeout_ms, cur)) break;
            float speed = ins_cruise_speed((float)ins->p2, dist_target, cur);
            /* 循线 PID */
            float line_err = -CalculateNormalizedValue(&g_GraySensor, 0U);
            float steer = PID_Calc(&pidLine, line_err);
            /* 转向量大时降速防甩出 */
            float drop = fabsf(steer) * 0.15f;
            if (drop > speed * 0.35f) drop = speed * 0.35f;
            speed -= drop;
            g_ble_target_left  = speed - steer;
            g_ble_target_right = speed + steer;
            if (cur >= dist_target) {
                do_stop();
                exec_state = INS_EXEC_IDLE;
                advance = true;
            }
            break;
        }

        case 0x21: { /* LINE_UNTIL_LOST: p1=timeout_ms(0=不限), p2=speed — 循线直到丢线 */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_encL = encoderLeft.total_count;
                ins_ctx.start_encR = encoderRight.total_count;
                ins_ctx.start_ms   = mspm0_get_clock_ms();
                ins_ctx.timeout_ms = (uint32_t)ins->p1;
                ins_ctx.last_dist  = 0;
                ins_ctx.stuck_frames = 0;
                ins_load_schedule((float)ins->p2);
                PID_Reset(&pidLine);
                PID_SetTarget(&pidLine, 0.0f);
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            int32_t cur = ins_seg_dist();
            if (!ins_motion_check(ins_ctx.timeout_ms, cur)) break;
            uint8_t gray = Get_Gray_Digital();
            if (gray == 0 && (mspm0_get_clock_ms() - ins_ctx.start_ms) > 300) {
                /* 丢线超过 300ms 才算真正丢线 (防抖) */
                do_stop();
                exec_state = INS_EXEC_IDLE;
                advance = true;
                break;
            }
            float speed = (float)ins->p2;
            float line_err = -CalculateNormalizedValue(&g_GraySensor, 0U);
            float steer = PID_Calc(&pidLine, line_err);
            float drop = fabsf(steer) * 0.15f;
            if (drop > speed * 0.35f) drop = speed * 0.35f;
            speed -= drop;
            g_ble_target_left  = speed - steer;
            g_ble_target_right = speed + steer;
            break;
        }

        case 0x22: { /* LINE_FOLLOW_TIME: p1=time_ms, p2=speed — 定时长循线 */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_encL = encoderLeft.total_count;
                ins_ctx.start_encR = encoderRight.total_count;
                ins_ctx.start_ms   = mspm0_get_clock_ms();
                ins_ctx.timeout_ms = (uint32_t)ins->p1;
                ins_ctx.last_dist  = 0;
                ins_ctx.stuck_frames = 0;
                ins_load_schedule((float)ins->p2);
                PID_Reset(&pidLine);
                PID_SetTarget(&pidLine, 0.0f);
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            /* 超时 = 正常完成 (定时长循线) */
            if (mspm0_get_clock_ms() - ins_ctx.start_ms >= ins_ctx.timeout_ms) {
                do_stop();
                exec_state = INS_EXEC_IDLE;
                advance = true;
                break;
            }
            /* 编码器卡死检测 (不走 ins_motion_check, 因超时语义不同) */
            int32_t cur = ins_seg_dist();
            if (cur <= ins_ctx.last_dist + 1) {
                if (++ins_ctx.stuck_frames > 100) {
                    ins_motion_error(INS_ERR_ENCODER);
                    break;
                }
            } else {
                ins_ctx.stuck_frames = 0;
            }
            ins_ctx.last_dist = cur;
            float speed = (float)ins->p2;
            float line_err = -CalculateNormalizedValue(&g_GraySensor, 0U);
            float steer = PID_Calc(&pidLine, line_err);
            float drop = fabsf(steer) * 0.15f;
            if (drop > speed * 0.35f) drop = speed * 0.35f;
            speed -= drop;
            g_ble_target_left  = speed - steer;
            g_ble_target_right = speed + steer;
            break;
        }

        case 0x23: { /* LINE_CURVE: p1=dist_cm*10, p2=speed — 弧线循线 (curve_kp/kd) */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_encL = encoderLeft.total_count;
                ins_ctx.start_encR = encoderRight.total_count;
                ins_ctx.start_ms   = mspm0_get_clock_ms();
                ins_ctx.timeout_ms = ins_est_timeout(ins->p1, (float)ins->p2);
                ins_ctx.last_dist  = 0;
                ins_ctx.stuck_frames = 0;
                ins_load_schedule((float)ins->p2);
                /* 弧线循线: 临时切到 curve 增益 */
                pidLine.kp             = ins_node.curve_kp;
                pidLine.kd             = ins_node.curve_kd;
                pidLine.output_limit_p =  ins_node.curve_max_steer;
                pidLine.output_limit_n = -ins_node.curve_max_steer;
                PID_Reset(&pidLine);
                PID_Reset(&pidPosition);
                PID_SetTarget(&pidLine, 0.0f);
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            int32_t dist_target = (int32_t)((float)ins->p1 * INS_PULSES_PER_CM / 10.0f);
            int32_t cur = ins_seg_dist();
            if (!ins_motion_check(ins_ctx.timeout_ms, cur)) break;
            float speed = ins_cruise_speed((float)ins->p2, dist_target, cur);
            /* 弧线循线: 大误差时触发 curve 增益, 小误差回 line 增益 */
            float line_err = -CalculateNormalizedValue(&g_GraySensor, 0U);
            float steer;
            if (fabsf(line_err) > ins_node.curve_error_trigger) {
                steer = PID_Calc(&pidLine, line_err);
            } else {
                /* 误差小: 临时切回 line 增益防抖 */
                float orig_kp = pidLine.kp, orig_kd = pidLine.kd;
                float orig_lp = pidLine.output_limit_p, orig_ln = pidLine.output_limit_n;
                pidLine.kp = ins_node.line_kp;
                pidLine.kd = ins_node.line_kd;
                pidLine.output_limit_p =  ins_node.line_max_steer;
                pidLine.output_limit_n = -ins_node.line_max_steer;
                steer = PID_Calc(&pidLine, line_err);
                pidLine.kp = orig_kp; pidLine.kd = orig_kd;
                pidLine.output_limit_p = orig_lp; pidLine.output_limit_n = orig_ln;
            }
            float drop = fabsf(steer) * 0.15f;
            if (drop > speed * 0.35f) drop = speed * 0.35f;
            speed -= drop;
            g_ble_target_left  = speed - steer;
            g_ble_target_right = speed + steer;
            if (cur >= dist_target) {
                do_stop();
                exec_state = INS_EXEC_IDLE;
                advance = true;
            }
            break;
        }

        /* ===== 传感器等待类 ===== */
        case 0x30: /* WAIT_BLACK */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_ms = mspm0_get_clock_ms();
                ins_ctx.timeout_ms = (uint32_t)ins->p1;
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            if (Get_Gray_Digital() != 0) { exec_state = INS_EXEC_IDLE; advance = true; }
            else if (mspm0_get_clock_ms() - ins_ctx.start_ms > ins_ctx.timeout_ms)
                { exec_state = INS_EXEC_IDLE; advance = true; /* timeout */ }
            break;

        case 0x31: /* WAIT_NOBLACK */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_ms = mspm0_get_clock_ms();
                ins_ctx.timeout_ms = (uint32_t)ins->p1;
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            if (Get_Gray_Digital() == 0) { exec_state = INS_EXEC_IDLE; advance = true; }
            else if (mspm0_get_clock_ms() - ins_ctx.start_ms > ins_ctx.timeout_ms)
                { exec_state = INS_EXEC_IDLE; advance = true; }
            break;

        case 0x33: /* STOP_IF_BLACK */
            if (Get_Gray_Digital() != 0) { do_stop(); }
            advance = true;
            break;

        /* ===== 速度/配置类 ===== */
        case 0x40: /* SET_BASE_SPEED */
            BLE_Param_Write(PARAM_BASE_SPEED, (float)ins->p1);
            advance = true; break;

        case 0x41: /* SET_SPEED_RAW */
            if (exec_state == INS_EXEC_IDLE) {
                g_ble_target_left  = (float)ins->p1 * 0.1f;
                g_ble_target_right = (float)ins->p2 * 0.1f;
                if (ins->p3 > 0) {
                    ins_ctx.start_ms = mspm0_get_clock_ms();
                    ins_ctx.timeout_ms = (uint32_t)ins->p3;
                    exec_state = INS_EXEC_RUNNING;
                } else {
                    advance = true;
                }
                break;
            }
            /* 定时完成检查 (SET_SPEED_RAW 专用, 不走 ins_motion_check) */
            if (ins_ctx.timeout_ms > 0 &&
                mspm0_get_clock_ms() - ins_ctx.start_ms >= ins_ctx.timeout_ms) {
                do_stop();
                exec_state = INS_EXEC_IDLE;
                advance = true;
            }
            break;

        case 0x52: /* YAW_RESET */
            ins_ctx.start_yaw = g_imuData.yaw;
            advance = true; break;

        case 0x53: /* ENC_RESET */
            ins_ctx.start_encL = encoderLeft.total_count;
            ins_ctx.start_encR = encoderRight.total_count;
            advance = true; break;

        case 0x54: /* DELAY */
            if (exec_state == INS_EXEC_IDLE) {
                ins_ctx.start_ms = mspm0_get_clock_ms();
                exec_state = INS_EXEC_RUNNING;
                break;
            }
            if (mspm0_get_clock_ms() - ins_ctx.start_ms >= (uint32_t)ins->p1)
                { exec_state = INS_EXEC_IDLE; advance = true; }
            break;

        case 0x56: /* BRAKE */
            do_stop();
            advance = true; break;

        /* ===== 控制流 ===== */
        case 0x60: /* LABEL — skip */
            advance = true; break;

        case 0x61: /* JUMP */
            {
                uint16_t target = find_label((uint8_t)ins->p1);
                if (target != 0xFFFF && target < ins_count) {
                    ins_ip = target;
                    continue;
                }
            }
            advance = true; break;

        case 0x62: /* JUMP_IF */
            if (eval_cond((uint8_t)ins->p2, ins->p3)) {
                uint16_t target = find_label((uint8_t)ins->p1);
                if (target != 0xFFFF && target < ins_count) {
                    ins_ip = target;
                    continue;
                }
            }
            advance = true; break;

        case 0x63: /* IF */
            if (!eval_cond((uint8_t)ins->p1, ins->p2)) {
                ins_ip = skip_to_else(ins_ip);
                continue;
            }
            advance = true; break;

        case 0x64: /* ELSE */
            ins_ip = skip_to_endif(ins_ip);
            continue;

        case 0x65: /* END_IF */
            advance = true; break;

        case 0x66: /* FOR: p1=var_id, p2=start, p3=end, p4=step */
            if (nest_sp >= NEST_MAX) break;
            {
                uint8_t vid = (uint8_t)ins->p1;
                if (vid < LOOP_VAR_COUNT) {
                    loop_vars[vid] = ins->p2;
                }
                nest_stack[nest_sp].type     = 0;
                nest_stack[nest_sp].loop_ip  = ins_ip;
                nest_stack[nest_sp].start_val = ins->p2;
                nest_stack[nest_sp].end_val  = ins->p3;
                nest_stack[nest_sp].step     = ins->p4;
                nest_stack[nest_sp].var_id   = vid;
                nest_sp++;
            }
            advance = true; break;

        case 0x67: { /* END_FOR */
            if (nest_sp == 0) { advance = true; break; }
            nest_t *nf = &nest_stack[nest_sp - 1];
            if (nf->type != 0) { advance = true; break; }
            uint8_t vid = nf->var_id;
            loop_vars[vid] += nf->step;
            if ((nf->step > 0 && loop_vars[vid] < nf->end_val) ||
                (nf->step < 0 && loop_vars[vid] > nf->end_val)) {
                ins_ip = nf->loop_ip + 1;
                continue;
            }
            nest_sp--;
            advance = true;
            break;
        }

        case 0x68: /* WHILE: p1=cond_type, p2=cond_val */
            if (nest_sp >= NEST_MAX) break;
            if (!eval_cond((uint8_t)ins->p1, ins->p2)) {
                ins_ip = skip_to_endwhile(ins_ip);
                continue;
            }
            nest_stack[nest_sp].type    = 1;
            nest_stack[nest_sp].loop_ip = ins_ip;
            nest_sp++;
            advance = true; break;

        case 0x69: /* END_WHILE */
            if (nest_sp > 0 && nest_stack[nest_sp-1].type == 1) {
                ins_ip = nest_stack[nest_sp-1].loop_ip;
                continue;
            }
            nest_sp = (nest_sp > 0) ? nest_sp - 1 : 0;
            advance = true; break;

        case 0x6A: /* BREAK */
            if (nest_sp > 0) {
                nest_sp--;
                if (nest_stack[nest_sp].type == 0)
                    ins_ip = find_endfor(nest_stack[nest_sp].loop_ip + 1);
                else
                    ins_ip = skip_to_endwhile(nest_stack[nest_sp].loop_ip + 1);
            }
            advance = true; break;

        case 0x6B: /* CONTINUE */
            if (nest_sp > 0) {
                nest_t *nf = &nest_stack[nest_sp-1];
                if (nf->type == 0) {
                    uint8_t vid = nf->var_id;
                    loop_vars[vid] += nf->step;
                }
                ins_ip = nf->loop_ip;
                continue;
            }
            advance = true; break;

        case 0x6F: /* END_SCRIPT */
            ins_running = false;
            g_ble_state = BLE_STATE_IDLE;
            do_stop();
            {
                uint8_t done = 1;
                BLE_Protocol_Send(BLE_CMD_INS_EVENT, &done, 1);
            }
            return;

        default:
            advance = true; break;
        }

        /* 单步模式 */
        if (advance && ins_paused) {
            advance = true;
            ins_paused = true;
        }

        if (advance) {
            ins_ip++;
            exec_state = INS_EXEC_IDLE;
        } else {
            /* 运动类指令在等待完成, 不阻塞主循环 */
            break;
        }
    }

    /* 指令执行完毕 */
    if (ins_ip >= ins_count) {
        ins_running = false;
        g_ble_state = BLE_STATE_IDLE;
        do_stop();
        uint8_t done = 1;
        BLE_Protocol_Send(BLE_CMD_INS_EVENT, &done, 1);
    }
}
