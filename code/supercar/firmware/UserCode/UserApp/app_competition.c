/**
 * @file      app_competition.c
 * @brief     通用竞赛任务框架 — 段表驱动 实现
 *
 * @details
 *   所有运动原语复用全局 PID + 三节点 schedule 插值, 与 ISR 速度内环构成两层架构:
 *     外环 (本文件, 10ms): 段表原语 → targetSpeedLeft/Right
 *     内环 (isr.c, 10ms):  速度 PID → PWM
 *
 *   无任何 2024 硬编码任务; 题目确定后通过 Competition_RegisterMission +
 *   Competition_AppendSegment 注册段表即可。
 */

#include "app_competition.h"

#include "encoder_driver.h"
#include "No_Mcu_Ganv_Grayscale_Sensor.h"
#include "pid.h"
#include "ble_param.h"
#include "app_imu.h"
#include "app_indicator.h"
#include "app_position_hold.h"
#include "app_ball_task3.h"
#include "sys_time.h"
#include "at4950.h"
#include "led.h"
#include "buzzer.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ==================== 外部硬件引用 ==================== */
extern Encoder_t     encoderLeft;
extern Encoder_t     encoderRight;
extern MOTOR_t       motorLeft;
extern MOTOR_t       motorRight;
extern PID_t         pidLeftSpeed;
extern PID_t         pidRightSpeed;
extern PID_t         pidLine;

extern PID_t         pidAngle;
extern PID_t         pidPosition;
extern No_MCU_Sensor g_GraySensor;

/* 速度目标 (empty.c 定义, ISR 读取) */
extern float targetSpeedLeft;
extern float targetSpeedRight;

extern uint8_t Get_Gray_Digital(void);
extern uint8_t g_gray_runtime_calibrated;

/* I2C0 分配策略 (定义在 empty.c)：正式比赛关闭IMU给OLED；
 * App实时循迹调试时反转策略，暂停OLED并保持IMU更新。 */
extern bool g_imu_enabled;
extern bool g_h1_app_debug_mode;
extern bool g_poshold_active;

/* ==================== 全局状态 ==================== */
CompetitionControl_t g_competition = {0};

/* Mission 表 */
static CompMission_t s_missions[COMP_MAX_MISSIONS];
static uint8_t       s_mission_count = 0;

/* 当前段的 schedule 快照 */
static pid_schedule_node_t s_seg_node;

/* H题 A 点横线检测状态：起步时先越过横线，之后才允许下一次横线结束任务。 */
static bool     s_marker_left_start = false;
static uint8_t  s_marker_confirm_samples = 0U;

/* H题循迹外环遥测快照。控制仍完全在MCU 10ms回路执行，App只读取。 */
static float    s_line_debug_error = 0.0f;
static float    s_line_debug_steer = 0.0f;
static float    s_line_debug_base_speed = 0.0f;
static uint8_t  s_line_debug_gray = 0U;
static uint8_t  s_line_debug_valid = 0U;

/* H题弯道调度：仍由实时灰度误差滞回切换，确保误差回到直线区时立即恢复原直线控制。 */
static bool     s_h1_curve_mode = false;
/* 带球任务的平滑起步与完成后的底盘后退状态。 */
static float    s_ball_start_speed_cm_s = 0.0f;
static uint32_t s_ball_start_last_ms = 0U;
/* T1到达指定脉冲后：先锁位刹停，再低速后退2cm，最后按STOP段完成。 */
static bool     s_t1_retreat_pending = false;
static bool     s_t1_retreat_active = false;
static int32_t  s_t1_retreat_start_encL = 0;
static int32_t  s_t1_retreat_start_encR = 0;
static uint32_t s_t1_retreat_start_ms = 0U;
/* T2滚球序列完成后的后退2cm。 */
static bool     s_t2_retreat_active = false;
static int32_t  s_t2_retreat_start_encL = 0;
static int32_t  s_t2_retreat_start_encR = 0;

/* H题灰度误差滤波：8路位图在弧顶交界可瞬间跳变，直接送入增量式PID会产生假转向脉冲。 */
static float    s_h1_line_error_filtered = 0.0f;
static float    s_h1_line_error_history[3] = {0.0f, 0.0f, 0.0f};
static uint8_t  s_h1_line_error_count = 0U;
static float    s_h1_steer_limited = 0.0f;
static bool     s_h1_line_filter_valid = false;
#define H1_LINE_ERROR_FILTER_ALPHA  0.45f
#define H1_LINE_ERROR_STEP_LIMIT     1200.0f
#define H1_STEER_STEP_LIMIT          0.8f
/* 已验证的空载任务2速度档：42/38cm/s约15秒；任务4/5滚球阶段使用低速恒定档。 */
#define H1_CURVE_SPEED_CM_S          38.0f
#define H1_MARKER_CROSS_SPEED_CM_S   24.0f
#define H1_BALL_LAP_SPEED_CM_S       24.0f
#define H1_BALL_LAP_TIMEOUT_MS       30000U
/* 带球任务起步限加速度；T3/T4均从0平滑升到段目标速度，减少球的后滚惯性。 */
#define H1_BALL_START_ACCEL_CM_S2    12.0f
/* 带球绕圈在A点前约35cm开始降速，最后以低速进入停车锁位。 */
#define H1_BALL_LAP_SLOWDOWN_PULSES  2496
#define H1_BALL_LAP_END_SPEED_CM_S    6.0f
/* T2滚珠序列完成、底盘静止后，编码器闭环缓慢后退2cm。 */
#define H1_T2_RETREAT_PULSES          143
#define H1_T2_RETREAT_MAX_SPEED_CM_S  8.0f
#define H1_T2_RETREAT_MIN_SPEED_CM_S  3.0f
/* T1指定脉冲停车后，先锁位350ms消除惯性，再低速后退约2cm。 */
#define H1_T1_RETREAT_PULSES          143
#define H1_T1_RETREAT_MAX_SPEED_CM_S  8.0f
#define H1_T1_RETREAT_MIN_SPEED_CM_S  3.0f
#define H1_T1_RETREAT_TIMEOUT_MS      3000U
#define H1_AB_DISTANCE_PULSES        10695
/* 近期42/38档回A前的平均行程约41942脉冲；提前3cm(214脉冲)锁位。 */
#define H1_LAP_PRESTOP_PULSES        41728
/* 曲线工作点偏置：当前试验确认正偏置会增加右转、错过A点横线，恢复为零。
 * 后续若重试必须单独验证符号与量级，不能与直线、A点候选控制混合。 */
#define H1_CURVE_ERROR_BIAS          0.0f

/* H题回A停车锁位：只在正常完成时短时启用，抑制刹车后的惯性前移。 */
static PosHold_t s_h1_hold_left;
static PosHold_t s_h1_hold_right;
static bool      s_h1_hold_active = false;
static bool      s_h1_hold_persistent = false;
static uint32_t  s_h1_hold_start_ms = 0U;

/* A点横线宽5cm，顺时针从弧线带偏航进入时实测会出现0xFC(6路黑)。
 * H1外环固定10ms执行，使用连续两帧确认替代时间比较：第二帧即同周期刹车，
 * 同时保留“离开起点横线后才允许触发”的保护，避免起步时误停。 */
#define COMP_MARKER_MIN_BLACK_BITS       6U
#define COMP_MARKER_CONFIRM_SAMPLES      2U
#define COMP_MARKER_ARM_TIMEOUT_MS       5000U
/* A点停车以前由6路黑确认后才制动：灰度模块已经深入横线，42cm/s下必然前冲。
 * 固定赛道允许在接近一圈末端、首次扫到横线前沿(至少4路黑)即原子制动；
 * 约40500脉冲的距离门限屏蔽两段圆弧内普通的4路灰度位型；该值低于近期A点
 * 6路确认的约41600~42000脉冲，仅给横线前沿和制动留出窗口。 */
#define H1_MARKER_PRESTOP_BLACK_BITS     4U
#define H1_MARKER_PRESTOP_MIN_PULSES     H1_LAP_PRESTOP_PULSES
#define H1_LAP_SPEED_CM_S           42.0f
#define H1_LAP_TIMEOUT_MS           20000U
#define H1_STOP_HOLD_MS              350U
#define H1_STOP_HOLD_KP              1.5f
#define H1_STOP_HOLD_KD              0.3f
#define H1_STOP_HOLD_DEADBAND        3
#define H1_STOP_HOLD_MAX_PWM         800

/* 每个 mission 通过 ball_mode 显式声明滚球需求，不能再依赖固定任务索引。
 * 运动滚球任务同时维持 IMU 更新，为后续 yawRate 补偿提供实时信号。 */

/* ==================== 辅助函数 ==================== */

static float comp_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float comp_relative_yaw(float current, float start)
{
    float v = current - start;
    while (v >  180.0f) v -= 360.0f;
    while (v < -180.0f) v += 360.0f;
    return v;
}

static uint8_t comp_count_black_bits(uint8_t gray)
{
    uint8_t count = 0U;
    while (gray != 0U) {
        count += (uint8_t)(gray & 0x01U);
        gray >>= 1;
    }
    return count;
}

static bool comp_marker_present(uint8_t gray)
{
    return comp_count_black_bits(gray) >= COMP_MARKER_MIN_BLACK_BITS;
}

static float comp_median3(float a, float b, float c)
{
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { float t = b; b = c; c = t; }
    if (a > b) { float t = a; a = b; b = t; }
    return b;
}

#define COMP_PULSES_PER_CM  71.3f

static int32_t comp_abs_diff(int32_t now, int32_t start)
{
    int32_t delta = now - start;
    return (delta < 0) ? -delta : delta;
}

static int32_t comp_seg_dist(void)
{
    int32_t dl = comp_abs_diff(encoderLeft.total_count, g_competition.seg_start_encL);
    int32_t dr = comp_abs_diff(encoderRight.total_count, g_competition.seg_start_encR);
    return (dl + dr) / 2;
}

void Competition_GetLapTravelPulses(int32_t *left, int32_t *right, int32_t *average)
{
    int32_t dl = comp_abs_diff(encoderLeft.total_count, g_competition.task_start_encL);
    int32_t dr = comp_abs_diff(encoderRight.total_count, g_competition.task_start_encR);
    if (left != 0) *left = dl;
    if (right != 0) *right = dr;
    if (average != 0) *average = (dl + dr) / 2;
}

static void comp_capture_h1_result(void)
{
    Competition_GetLapTravelPulses(&g_competition.lap_finish_encL,
                                   &g_competition.lap_finish_encR, 0);
    g_competition.task_elapsed_ms = mspm0_get_clock_ms() - g_competition.task_start_ms;
}

void Competition_GetH1ResultSnapshot(uint8_t *state, uint32_t *elapsed_ms,
                                     int32_t *left_pulses, int32_t *right_pulses,
                                     uint8_t *max_black_bits)
{
    int32_t left = g_competition.lap_finish_encL;
    int32_t right = g_competition.lap_finish_encR;
    uint32_t elapsed = g_competition.task_elapsed_ms;
    if (g_competition.h1_result == H1_RUN_RUNNING) {
        Competition_GetLapTravelPulses(&left, &right, 0);
        elapsed = mspm0_get_clock_ms() - g_competition.task_start_ms;
    }
    if (state != 0) *state = (uint8_t)g_competition.h1_result;
    if (elapsed_ms != 0) *elapsed_ms = elapsed;
    if (left_pulses != 0) *left_pulses = left;
    if (right_pulses != 0) *right_pulses = right;
    if (max_black_bits != 0) *max_black_bits = g_competition.lap_marker_max_black_bits;
}

static void comp_print_lap_travel(const char *reason)
{
    int32_t left, right, average;
    Competition_GetLapTravelPulses(&left, &right, &average);
    printf("H1 travel [%s]: L=%ld (%.1fcm), R=%ld (%.1fcm), AVG=%ld (%.1fcm)\r\n",
           reason, (long)left, (double)left / COMP_PULSES_PER_CM,
           (long)right, (double)right / COMP_PULSES_PER_CM,
           (long)average, (double)average / COMP_PULSES_PER_CM);
}

static void comp_load_schedule(float speed, uint8_t node_index)
{
    if (node_index < PID_SCHEDULE_NODE_COUNT) {
        const pid_schedule_node_t *nodes = BLE_Param_GetPidSchedule();
        s_seg_node = nodes[node_index];
    } else {
        BLE_Param_InterpolatePidSchedule(speed, &s_seg_node);
    }
    pidLine.kp             = s_seg_node.line_kp;
    pidLine.kd             = s_seg_node.line_kd;
    pidLine.output_limit_p =  s_seg_node.line_max_steer;
    pidLine.output_limit_n = -s_seg_node.line_max_steer;
    pidAngle.kp            = s_seg_node.yaw_kp;
    pidAngle.kd            = s_seg_node.yaw_kd;
    pidAngle.output_limit_p =  s_seg_node.yaw_max_steer;
    pidAngle.output_limit_n = -s_seg_node.yaw_max_steer;
    pidPosition.kp         = s_seg_node.pos_kp;
}

static float comp_ball_smooth_start_speed(float requested_speed)
{
    uint32_t now;
    uint32_t dt_ms;
    float step;

    if (Competition_GetBallMode() != COMP_BALL_MODE_MOTION_BALANCE ||
        requested_speed <= 0.0f) return requested_speed;

    now = mspm0_get_clock_ms();
    if (s_ball_start_last_ms == 0U) {
        s_ball_start_last_ms = now;
        s_ball_start_speed_cm_s = 0.0f;
    }
    dt_ms = now - s_ball_start_last_ms;
    s_ball_start_last_ms = now;
    step = H1_BALL_START_ACCEL_CM_S2 * ((float)dt_ms * 0.001f);
    if (s_ball_start_speed_cm_s < requested_speed) {
        s_ball_start_speed_cm_s += step;
        if (s_ball_start_speed_cm_s > requested_speed) {
            s_ball_start_speed_cm_s = requested_speed;
        }
    } else if (s_ball_start_speed_cm_s > requested_speed) {
        /* 终点减速由位置/圈末速度规划给出，不能被起步斜坡重新抬高。 */
        s_ball_start_speed_cm_s = requested_speed;
    }
    return s_ball_start_speed_cm_s;
}

static float comp_ball_lap_speed(float cruise)
{
    int32_t lap_pulses = 0;
    int32_t slow_start;
    float ratio;

    if (Competition_GetBallMode() != COMP_BALL_MODE_MOTION_BALANCE) return cruise;

    Competition_GetLapTravelPulses(0, 0, &lap_pulses);
    slow_start = H1_LAP_PRESTOP_PULSES - H1_BALL_LAP_SLOWDOWN_PULSES;
    if (lap_pulses <= slow_start) return cruise;
    if (lap_pulses >= H1_LAP_PRESTOP_PULSES) return H1_BALL_LAP_END_SPEED_CM_S;

    ratio = (float)(lap_pulses - slow_start) /
            (float)H1_BALL_LAP_SLOWDOWN_PULSES;
    return cruise - (cruise - H1_BALL_LAP_END_SPEED_CM_S) * ratio;
}

static float comp_cruise_speed(float cruise, int32_t dist_target, int32_t dist_done)
{
    int32_t remaining = dist_target - dist_done;
    if (remaining <= 0) return s_seg_node.end_speed;

    PID_SetTarget(&pidPosition, 0.0f);
    float pos_speed = PID_Calc(&pidPosition, (float)(-remaining));
    if (pos_speed > cruise)             pos_speed = cruise;
    if (pos_speed < s_seg_node.end_speed) pos_speed = s_seg_node.end_speed;

    int32_t decel = (int32_t)s_seg_node.decel_pulses;
    if (decel > 200 && remaining < decel) {
        float ratio = (float)remaining / (float)decel;
        float profile = s_seg_node.end_speed + (cruise - s_seg_node.end_speed) * ratio;
        if (profile < pos_speed) pos_speed = profile;
    }
    return pos_speed;
}

static void comp_set_speeds(float left, float right)
{
    targetSpeedLeft  = left;
    targetSpeedRight = right;
}

static void comp_hold_cancel(void)
{
    g_poshold_active = false;
    s_h1_hold_active = false;
    s_h1_hold_persistent = false;
    PosHold_Disable(&s_h1_hold_left);
    PosHold_Disable(&s_h1_hold_right);
}

static void comp_brake(void)
{
    comp_hold_cancel();
    Motor_SetSpeed(&motorLeft, 0);
    Motor_SetSpeed(&motorRight, 0);
    Motor_Brake(&motorLeft);
    Motor_Brake(&motorRight);
    targetSpeedLeft  = 0.0f;
    targetSpeedRight = 0.0f;
}

static void comp_hold_start(void)
{
    /* 锁定电刹刚完成时的位置，ISR会跳过速度PID，由此处直接给反向补偿PWM。 */
    PosHold_Enable(&s_h1_hold_left, encoderLeft.total_count);
    PosHold_Enable(&s_h1_hold_right, encoderRight.total_count);
    s_h1_hold_start_ms = mspm0_get_clock_ms();
    s_h1_hold_active = true;
    g_poshold_active = true;
}

static void comp_h1_stop_now(void);

static bool comp_t1_retreat_task(void)
{
    int32_t dl;
    int32_t dr;
    int32_t dist;
    int32_t remaining;
    float speed;

    if (!s_t1_retreat_active) return false;
    if (mspm0_get_clock_ms() - s_t1_retreat_start_ms > H1_T1_RETREAT_TIMEOUT_MS) {
        printf("H1 T1: 2cm retreat timeout.\r\n");
        s_t1_retreat_active = false;
        Competition_SetFailed();
        return false;
    }
    dl = comp_abs_diff(encoderLeft.total_count, s_t1_retreat_start_encL);
    dr = comp_abs_diff(encoderRight.total_count, s_t1_retreat_start_encR);
    dist = (dl + dr) / 2;
    remaining = H1_T1_RETREAT_PULSES - dist;
    if (remaining <= 0) {
        /* 后退到位后重新原子刹停锁位；接着由已进入的STOP段完成任务。 */
        comp_h1_stop_now();
        s_t1_retreat_active = false;
        return true;
    }

    speed = H1_T1_RETREAT_MIN_SPEED_CM_S +
            (H1_T1_RETREAT_MAX_SPEED_CM_S - H1_T1_RETREAT_MIN_SPEED_CM_S) *
            ((float)remaining / (float)H1_T1_RETREAT_PULSES);
    if (speed > H1_T1_RETREAT_MAX_SPEED_CM_S) speed = H1_T1_RETREAT_MAX_SPEED_CM_S;
    comp_set_speeds(-speed, -speed);
    return false;
}

static bool comp_t2_retreat_task(void)
{
    int32_t dl;
    int32_t dr;
    int32_t dist;
    int32_t remaining;
    float speed;

    if (!s_t2_retreat_active) return false;
    dl = comp_abs_diff(encoderLeft.total_count, s_t2_retreat_start_encL);
    dr = comp_abs_diff(encoderRight.total_count, s_t2_retreat_start_encR);
    dist = (dl + dr) / 2;
    remaining = H1_T2_RETREAT_PULSES - dist;
    if (remaining <= 0) {
        comp_h1_stop_now();
        s_h1_hold_persistent = true;
        s_t2_retreat_active = false;
        return true;
    }

    speed = H1_T2_RETREAT_MIN_SPEED_CM_S +
            (H1_T2_RETREAT_MAX_SPEED_CM_S - H1_T2_RETREAT_MIN_SPEED_CM_S) *
            ((float)remaining / (float)H1_T2_RETREAT_PULSES);
    if (speed > H1_T2_RETREAT_MAX_SPEED_CM_S) speed = H1_T2_RETREAT_MAX_SPEED_CM_S;
    /* 负速度为底盘后退；距离仍使用两轮绝对编码器增量计量。 */
    comp_set_speeds(-speed, -speed);
    return false;
}

static void comp_h1_stop_now(void)
{
    /* A点确认后的原子停车切换：电刹、速度环清零、当前位置锁位同周期完成。
     * 禁止先推进到STOP段、下一主循环才锁位，避免多出一个10ms惯性窗口。 */
    comp_brake();
    PID_Reset(&pidLeftSpeed);
    PID_Reset(&pidRightSpeed);
    comp_hold_start();
}

static bool comp_hold_task(void)
{
    if (!s_h1_hold_active) return true;

    Motor_SetSpeed(&motorLeft, PosHold_Update(&s_h1_hold_left, encoderLeft.total_count));
    Motor_SetSpeed(&motorRight, PosHold_Update(&s_h1_hold_right, encoderRight.total_count));

    if (s_h1_hold_persistent ||
        mspm0_get_clock_ms() - s_h1_hold_start_ms < H1_STOP_HOLD_MS) return false;

    comp_hold_cancel();
    Motor_SetSpeed(&motorLeft, 0);
    Motor_SetSpeed(&motorRight, 0);
    Motor_Brake(&motorLeft);
    Motor_Brake(&motorRight);
    return true;
}

static void comp_seg_advance(void)
{
    g_competition.current_seg++;
    g_competition.seg_start_ms  = mspm0_get_clock_ms();
    g_competition.seg_start_encL = encoderLeft.total_count;
    g_competition.seg_start_encR = encoderRight.total_count;
    g_competition.seg_start_yaw  = g_imuData.yaw;
    PID_Reset(&pidLine);
    PID_Reset(&pidAngle);
    PID_Reset(&pidPosition);
    s_marker_left_start = false;
    s_marker_confirm_samples = 0U;
}

/* ==================== 内置 H题单圈 mission 注册 ==================== */

static void register_debug_missions(void)
{
    uint8_t idx;
    CompSegment_t seg;

    /* T1 / 题目任务2：空载顺时针循线一圈，指定脉冲前置停车并复核A点横线。 */
    idx = Competition_RegisterMission("T1_LAP_STOP");
    if (idx == 0xFF) return;
    s_missions[idx].ball_mode = COMP_BALL_MODE_OFF;
    memset(&seg, 0, sizeof(seg));
    seg.prim = COMP_PRIM_LINE_UNTIL_MARKER;
    seg.speed = H1_LAP_SPEED_CM_S;
    seg.timeout_ms = H1_LAP_TIMEOUT_MS;
    seg.node_index = 255;
    Competition_AppendSegment(idx, &seg);
    memset(&seg, 0, sizeof(seg));
    seg.prim = COMP_PRIM_STOP;
    Competition_AppendSegment(idx, &seg);

    /* T2 / 题目任务3：车辆静止锁位，同时执行滚珠 O -> +5cm -> -5cm 序列。 */
    idx = Competition_RegisterMission("T2_STILL_HOLD");
    if (idx == 0xFF) return;
    s_missions[idx].ball_mode = COMP_BALL_MODE_STATIC_SEQUENCE;
    memset(&seg, 0, sizeof(seg));
    seg.prim = COMP_PRIM_HOLD_POSITION;
    seg.timeout_ms = 0U;
    Competition_AppendSegment(idx, &seg);

    /* T3 / 题目任务4：A→B=1.5m。灰度循迹负责横向，编码器位置外环负责
     * 终点距离；行驶过程要求滚珠持续保持在物理中心。 */
    idx = Competition_RegisterMission("T3_A_TO_B");
    if (idx == 0xFF) return;
    s_missions[idx].ball_mode = COMP_BALL_MODE_MOTION_BALANCE;
    memset(&seg, 0, sizeof(seg));
    seg.prim = COMP_PRIM_LINE_FOLLOW;
    seg.speed = 30.0f;
    seg.dist_pulses = H1_AB_DISTANCE_PULSES;
    seg.timeout_ms = 8000U;
    seg.node_index = 1U;
    Competition_AppendSegment(idx, &seg);
    memset(&seg, 0, sizeof(seg));
    seg.prim = COMP_PRIM_STOP;
    Competition_AppendSegment(idx, &seg);

    /* T4 / 题目任务5：全程低速循线一圈到A停车，行驶中持续启用滚球平衡。 */
    idx = Competition_RegisterMission("T4_BALL_LAP");
    if (idx == 0xFF) return;
    s_missions[idx].ball_mode = COMP_BALL_MODE_MOTION_BALANCE;
    memset(&seg, 0, sizeof(seg));
    seg.prim = COMP_PRIM_LINE_UNTIL_MARKER;
    seg.speed = H1_BALL_LAP_SPEED_CM_S;
    seg.timeout_ms = H1_BALL_LAP_TIMEOUT_MS;
    seg.node_index = 0U;
    Competition_AppendSegment(idx, &seg);
    memset(&seg, 0, sizeof(seg));
    seg.prim = COMP_PRIM_STOP;
    Competition_AppendSegment(idx, &seg);

    /* T5：与T4同一低扰动整圈底盘基线，明确保留为空载对照任务。 */
    idx = Competition_RegisterMission("T5_LAP_BASE");
    if (idx == 0xFF) return;
    s_missions[idx].ball_mode = COMP_BALL_MODE_OFF;
    memset(&seg, 0, sizeof(seg));
    seg.prim = COMP_PRIM_LINE_UNTIL_MARKER;
    seg.speed = H1_BALL_LAP_SPEED_CM_S;
    seg.timeout_ms = H1_BALL_LAP_TIMEOUT_MS;
    seg.node_index = 0U;
    Competition_AppendSegment(idx, &seg);
    memset(&seg, 0, sizeof(seg));
    seg.prim = COMP_PRIM_STOP;
    Competition_AppendSegment(idx, &seg);
}

/* ==================== 对外 API ==================== */

void Competition_Init(void)
{
    memset(&g_competition, 0, sizeof(g_competition));
    memset(s_missions, 0, sizeof(s_missions));
    s_mission_count = 0;
    s_marker_left_start = false;
    s_marker_confirm_samples = 0U;
    PosHold_Init(&s_h1_hold_left, H1_STOP_HOLD_KP, H1_STOP_HOLD_KD,
                 H1_STOP_HOLD_DEADBAND, H1_STOP_HOLD_MAX_PWM);
    PosHold_Init(&s_h1_hold_right, H1_STOP_HOLD_KP, H1_STOP_HOLD_KD,
                 H1_STOP_HOLD_DEADBAND, H1_STOP_HOLD_MAX_PWM);
    comp_hold_cancel();
    register_debug_missions();
    printf("Competition: %d missions registered.\r\n", (int)s_mission_count);
}

uint8_t Competition_RegisterMission(const char *name)
{
    if (s_mission_count >= COMP_MAX_MISSIONS) return 0xFF;
    uint8_t idx = s_mission_count;
    CompMission_t *m = &s_missions[idx];
    memset(m, 0, sizeof(CompMission_t));
    if (name) {
        strncpy(m->name, name, sizeof(m->name) - 1);
        m->name[sizeof(m->name) - 1] = '\0';
    }
    m->used = true;
    s_mission_count++;
    return idx;
}

bool Competition_AppendSegment(uint8_t mission_idx, const CompSegment_t *seg)
{
    if (mission_idx >= s_mission_count || !s_missions[mission_idx].used) return false;
    CompMission_t *m = &s_missions[mission_idx];
    if (m->seg_count >= COMP_MAX_SEGMENTS) return false;
    m->segments[m->seg_count++] = *seg;
    return true;
}

bool Competition_ClearMission(uint8_t mission_idx)
{
    if (mission_idx >= s_mission_count || !s_missions[mission_idx].used) return false;
    s_missions[mission_idx].seg_count = 0;
    return true;
}

uint8_t Competition_GetMissionCount(void) { return s_mission_count; }

const char *Competition_GetMissionName(uint8_t mission_idx)
{
    if (mission_idx >= s_mission_count) return "";
    return s_missions[mission_idx].name;
}

/* ---- 旧 API 兼容 ---- */

void Competition_SelectTask(CompetitionTask_e task)
{
    if (g_competition.state == COMP_STATE_RUNNING) return;
    if (task < COMP_TASK_1 || (int)task > (int)s_mission_count) return;

    /* 任务切换必须释放上一任务留下的滚球目标和底盘位置保持。
     * 特别是 T2 正常完成后会刻意保留 -5cm 与 persistent hold；若此处
     * 不清理，下一任务 READY 页面仍会继承旧执行器状态。 */
    BallTask3_Stop();
    s_t1_retreat_pending = false;
    s_t1_retreat_active = false;
    s_t2_retreat_active = false;
    comp_brake();
    PID_Reset(&pidLeftSpeed);
    PID_Reset(&pidRightSpeed);
    PID_Reset(&pidLine);
    PID_Reset(&pidAngle);
    PID_Reset(&pidPosition);
    s_line_debug_valid = 0U;
    s_line_debug_base_speed = 0.0f;
    App_Indicator_SetMissionRunning(false);
    App_Indicator_SetRedBlink(false);
    g_imu_enabled = true;

    g_competition.selected_mission = (uint8_t)task - 1;
    g_competition.state = COMP_STATE_IDLE;
    g_competition.emergency_stop = false;
    g_competition.task_start_ms = 0U;
    g_competition.task_elapsed_ms = 0U;
    printf("Mission selected: %d (%s)\r\n", (int)task,
           Competition_GetMissionName(g_competition.selected_mission));
}

void Competition_PrevTask(void)
{
    if (g_competition.state == COMP_STATE_RUNNING) return;
    if (s_mission_count == 0) return;
    int t = (int)g_competition.selected_mission - 1;
    if (t < 0) t = (int)s_mission_count - 1;
    Competition_SelectTask((CompetitionTask_e)(t + 1));
}

void Competition_NextTask(void)
{
    if (g_competition.state == COMP_STATE_RUNNING) return;
    if (s_mission_count == 0) return;
    int t = (int)g_competition.selected_mission + 1;
    if (t >= (int)s_mission_count) t = 0;
    Competition_SelectTask((CompetitionTask_e)(t + 1));
}

void Competition_Start(void)
{
    if (g_competition.state == COMP_STATE_RUNNING) return;
    if (s_mission_count == 0) return;
    CompMission_t *m = &s_missions[g_competition.selected_mission];
    if (!m->used || m->seg_count == 0) {
        printf("Competition: empty mission, abort.\r\n");
        return;
    }
    if ((m->ball_mode != COMP_BALL_MODE_STATIC_SEQUENCE) &&
        !g_gray_runtime_calibrated) {
        printf("Competition: gray calibration required.\r\n");
        return;
    }
    comp_hold_cancel();
    g_competition.state           = COMP_STATE_RUNNING;
    g_competition.current_seg     = 0;
    g_competition.emergency_stop  = false;
    g_competition.task_elapsed_ms = 0;

    /* 不清零编码器累计计数，避免破坏速度 ISR 的连续采样；段内距离统一用起始快照计算。 */
    g_competition.seg_start_ms   = mspm0_get_clock_ms();
    g_competition.seg_start_encL = encoderLeft.total_count;
    g_competition.seg_start_encR = encoderRight.total_count;
    g_competition.seg_start_yaw  = g_imuData.yaw;
    g_competition.task_start_ms  = mspm0_get_clock_ms();
    g_competition.task_start_encL = encoderLeft.total_count;
    g_competition.task_start_encR = encoderRight.total_count;
    g_competition.lap_finish_encL = 0;
    g_competition.lap_finish_encR = 0;
    g_competition.lap_marker_max_black_bits = 0U;
    g_competition.h1_result = H1_RUN_RUNNING;
    if (m->ball_mode == COMP_BALL_MODE_STATIC_SEQUENCE) {
        BallTask3_Start(g_competition.task_start_ms);
    } else {
        /* 运动滚球任务只保持中心，不进入 O -> +5cm -> -5cm 序列。 */
        BallTask3_Stop();
    }

    PID_Reset(&pidLine);
    PID_Reset(&pidAngle);
    PID_Reset(&pidPosition);
    s_marker_left_start = false;
    s_marker_confirm_samples = 0U;
    s_line_debug_error = 0.0f;
    s_line_debug_steer = 0.0f;
    s_line_debug_base_speed = 0.0f;
    s_line_debug_gray = 0U;
    s_line_debug_valid = 0U;
    s_h1_curve_mode = false;
    s_h1_line_error_filtered = 0.0f;
    s_h1_line_error_history[0] = 0.0f;
    s_h1_line_error_history[1] = 0.0f;
    s_h1_line_error_history[2] = 0.0f;
    s_h1_line_error_count = 0U;
    s_h1_steer_limited = 0.0f;
    s_h1_line_filter_valid = false;
    s_ball_start_speed_cm_s = 0.0f;
    s_ball_start_last_ms = mspm0_get_clock_ms();
    s_t1_retreat_pending = false;
    s_t1_retreat_active = false;
    s_t2_retreat_active = false;
    comp_load_schedule(m->segments[0].speed, m->segments[0].node_index);
    PID_SetTarget(&pidLine, 0.0f);
    PID_Reset(&pidLeftSpeed);
    PID_Reset(&pidRightSpeed);

    Motor_Brake(&motorLeft);
    Motor_Brake(&motorRight);

    App_Indicator_SetMissionRunning(true);
    App_Indicator_SetRedBlink(false);
    /* 有滚球运动补偿的任务必须保持 IMU 更新，以获取实时 yawRate；其余正式
     * 任务仍沿用 OLED 独占 I2C0 的策略。 */
    g_imu_enabled = g_h1_app_debug_mode ||
                    (m->ball_mode == COMP_BALL_MODE_MOTION_BALANCE);
    printf("=== Mission '%s' Started (%d segments) ===\r\n", m->name, (int)m->seg_count);
}

void Competition_Stop(void)
{
    if (g_competition.state == COMP_STATE_RUNNING) {
        comp_capture_h1_result();
        g_competition.h1_result = H1_RUN_MANUAL_STOP;
    }
    BallTask3_Stop();
    s_t1_retreat_pending = false;
    s_t1_retreat_active = false;
    s_t2_retreat_active = false;
    s_line_debug_valid = 0U;
    s_line_debug_base_speed = 0.0f;
    g_competition.state          = COMP_STATE_IDLE;
    g_competition.emergency_stop = true;
    comp_brake();
    PID_Reset(&pidLeftSpeed);
    PID_Reset(&pidRightSpeed);
    PID_Reset(&pidLine);
    PID_Reset(&pidAngle);
    PID_Reset(&pidPosition);
    led_off(&led_green);
    App_Indicator_SetMissionRunning(false);
    App_Indicator_SetRedBlink(false);
    /* 比赛结束: 恢复 IMU 常规更新 */
    g_imu_enabled = true;
    printf("=== Competition Stopped ===\r\n");
}

void Competition_SetFailed(void)
{
    comp_capture_h1_result();
    g_competition.h1_result = H1_RUN_TIMEOUT;
    comp_print_lap_travel("failed");

    /* 失败属于安全收尾：滚球目标回到物理中心，底盘释放旧位置保持并电刹。
     * 只有 T2 正常完成路径才允许继续保持 -5cm 终点。 */
    BallTask3_Stop();
    s_t1_retreat_pending = false;
    s_t1_retreat_active = false;
    s_t2_retreat_active = false;
    s_line_debug_valid = 0U;
    s_line_debug_base_speed = 0.0f;
    g_competition.state = COMP_STATE_FAILED;
    comp_brake();
    PID_Reset(&pidLeftSpeed);
    PID_Reset(&pidRightSpeed);
    PID_Reset(&pidLine);
    PID_Reset(&pidAngle);
    PID_Reset(&pidPosition);
    App_Indicator_SetMissionRunning(false);
    App_Indicator_SetRedBlink(true);
    App_Indicator_RequestBeepCount(3);
    /* 比赛失败: 恢复 IMU 常规更新 */
    g_imu_enabled = true;
    printf("!!! Mission FAILED !!!\r\n");
}

CompetitionTask_e  Competition_GetSelectedTask(void) { return (CompetitionTask_e)(g_competition.selected_mission + 1); }
CompetitionState_e Competition_GetState(void)        { return g_competition.state; }
CompBallMode_e Competition_GetBallMode(void)
{
    if (g_competition.selected_mission >= s_mission_count) return COMP_BALL_MODE_OFF;
    return s_missions[g_competition.selected_mission].ball_mode;
}
uint32_t           Competition_GetElapsedMs(void)    { return g_competition.task_elapsed_ms; }
uint8_t            Competition_GetSelectedMissionIndex(void) { return g_competition.selected_mission; }

void Competition_GetLineDebugSnapshot(float *line_error, float *line_steer,
                                      float *base_speed, uint8_t *gray,
                                      uint8_t *valid)
{
    if (line_error != 0) *line_error = s_line_debug_error;
    if (line_steer != 0) *line_steer = s_line_debug_steer;
    if (base_speed != 0) *base_speed = s_line_debug_base_speed;
    if (gray != 0) *gray = s_line_debug_gray;
    if (valid != 0) *valid = s_line_debug_valid;
}

/* ==================== 段表原语执行 ==================== */

static void exec_segment(CompMission_t *m)
{
    if (g_competition.current_seg >= m->seg_count) {
        /* 全部段完成 */
        comp_brake();
        s_line_debug_valid = 0U;
        s_line_debug_base_speed = 0.0f;
        g_competition.state = COMP_STATE_FINISHED;
        g_competition.task_elapsed_ms = mspm0_get_clock_ms() - g_competition.task_start_ms;
        App_Indicator_RequestLongBeep();
        App_Indicator_SetMissionRunning(false);
        /* 比赛正常完成: 恢复 IMU 常规更新 */
        g_imu_enabled = true;
        printf("=== Mission Complete! Time: %ums ===\r\n", g_competition.task_elapsed_ms);
        return;
    }

    CompSegment_t *seg = &m->segments[g_competition.current_seg];
    uint32_t now = mspm0_get_clock_ms();
    uint32_t seg_elapsed = now - g_competition.seg_start_ms;

    /* 超时保护 */
    if (seg->timeout_ms > 0 && seg_elapsed > seg->timeout_ms) {
        printf("Seg %d timeout!\r\n", (int)g_competition.current_seg);
        Competition_SetFailed();
        return;
    }

    /* 首次进入: 加载 schedule + 记录起始 */
    bool first_entry = (seg_elapsed < 15);  /* 第一帧 */
    if (first_entry) {
        comp_load_schedule(seg->speed, seg->node_index);
        PID_Reset(&pidLine);
        PID_Reset(&pidAngle);
        PID_Reset(&pidPosition);
        s_h1_curve_mode = false;
    }

    switch (seg->prim) {

    case COMP_PRIM_LINE_FOLLOW: {
        if (first_entry) {
            PID_SetTarget(&pidLine, 0.0f);
        }
        int32_t dist = comp_seg_dist();
        float speed = comp_cruise_speed(seg->speed, seg->dist_pulses, dist);
        speed = comp_ball_smooth_start_speed(speed);
        float line_err = -CalculateNormalizedValue(&g_GraySensor, 0U);
        float steer = PID_Calc(&pidLine, line_err);
        float drop = fabsf(steer) * 0.15f;
        if (drop > speed * 0.35f) drop = speed * 0.35f;
        speed -= drop;
        comp_set_speeds(speed - steer, speed + steer);
        if (dist >= seg->dist_pulses && seg_elapsed > 300) {
            App_Indicator_RequestBeepOnce();
            comp_seg_advance();
        }
        break;
    }

    case COMP_PRIM_YAW_HOLD: {
        if (first_entry) {
            PID_SetTarget(&pidAngle, 0.0f);
        }
        int32_t dist = comp_seg_dist();
        float speed = comp_cruise_speed(seg->speed, seg->dist_pulses, dist);
        float yaw_err = comp_relative_yaw(g_imuData.yaw, g_competition.seg_start_yaw);
        float steer = 0.0f;
        if (g_imuData.hw_ok) {
            steer = PID_Calc(&pidAngle, yaw_err);
            steer = comp_clampf(steer, -speed * 0.5f, speed * 0.5f);
        }
        comp_set_speeds(speed - steer, speed + steer);
        if (dist >= seg->dist_pulses && seg_elapsed > 300) {
            App_Indicator_RequestBeepOnce();
            comp_seg_advance();
        }
        break;
    }

    case COMP_PRIM_DRIVE_DISTANCE: {
        if (first_entry) {
            PID_SetTarget(&pidAngle, 0.0f);
        }
        int32_t dist = comp_seg_dist();
        float speed = comp_cruise_speed(seg->speed, seg->dist_pulses, dist);
        /* 无循线: 若有 IMU 做航向保持, 否则纯直行 */
        float steer = 0.0f;
        if (g_imuData.hw_ok) {
            float yaw_err = comp_relative_yaw(g_imuData.yaw, g_competition.seg_start_yaw);
            steer = PID_Calc(&pidAngle, yaw_err);
            steer = comp_clampf(steer, -speed * 0.5f, speed * 0.5f);
        }
        comp_set_speeds(speed - steer, speed + steer);
        if (dist >= seg->dist_pulses && seg_elapsed > 300) {
            App_Indicator_RequestBeepOnce();
            comp_seg_advance();
        }
        break;
    }

    case COMP_PRIM_TURN_YAW: {
        if (first_entry) {
            float target = g_competition.seg_start_yaw + seg->yaw_delta;
            PID_SetTarget(&pidAngle, target);
        }
        float actual_delta = comp_relative_yaw(g_imuData.yaw, g_competition.seg_start_yaw);
        float steer = PID_Calc(&pidAngle, g_imuData.yaw);
        float turn_speed = comp_clampf(steer, -seg->speed, seg->speed);
        /* 接近目标降速防过冲 */
        float remaining = fabsf(seg->yaw_delta - actual_delta);
        if (remaining < 5.0f && fabsf(turn_speed) > seg->speed * 0.4f)
            turn_speed = (turn_speed > 0) ? seg->speed * 0.4f : -seg->speed * 0.4f;
        comp_set_speeds(-turn_speed, turn_speed);
        if ((seg->yaw_delta > 0 && actual_delta >= seg->yaw_delta) ||
            (seg->yaw_delta < 0 && actual_delta <= seg->yaw_delta)) {
            comp_set_speeds(0.0f, 0.0f);
            App_Indicator_RequestBeepOnce();
            comp_seg_advance();
        }
        break;
    }

    case COMP_PRIM_WAIT: {
        comp_set_speeds(0.0f, 0.0f);
        if (seg_elapsed >= seg->wait_ms) {
            comp_seg_advance();
        }
        break;
    }

    case COMP_PRIM_HOLD_POSITION: {
        /* T2：底盘静止保持。位置目标只在进入段时捕获，直到KEY2安全停止。 */
        if (!s_h1_hold_active) {
            comp_h1_stop_now();
            s_h1_hold_persistent = true;
        } else {
            (void)comp_hold_task();
        }
        break;
    }

    case COMP_PRIM_STOP: {
        if (!s_h1_hold_active) {
            /* 非H1或未预先切换的STOP路径仍采用同一原子停车逻辑。 */
            comp_h1_stop_now();
            break;
        }
        if (!comp_hold_task()) break;

        s_line_debug_valid = 0U;
        s_line_debug_base_speed = 0.0f;
        g_competition.state = COMP_STATE_FINISHED;
        g_competition.task_elapsed_ms = mspm0_get_clock_ms() - g_competition.task_start_ms;
        App_Indicator_RequestLongBeep();
        App_Indicator_SetMissionRunning(false);
        /* STOP 原语: 恢复 IMU 常规更新 */
        g_imu_enabled = true;
        printf("=== Mission Complete! Time: %ums ===\r\n", g_competition.task_elapsed_ms);
        break;
    }

    case COMP_PRIM_LINE_UNTIL_LOST: {
        if (first_entry) {
            PID_SetTarget(&pidLine, 0.0f);
        }
        uint8_t gray = Get_Gray_Digital();
        if (gray == 0 && seg_elapsed > 500) {
            /* 丢线 → 段完成 */
            comp_set_speeds(0.0f, 0.0f);
            App_Indicator_RequestBeepOnce();
            comp_seg_advance();
        } else {
            float line_err = -CalculateNormalizedValue(&g_GraySensor, 0U);
            float steer = PID_Calc(&pidLine, line_err);
            float speed = seg->speed;
            float drop = fabsf(steer) * 0.15f;
            if (drop > speed * 0.35f) drop = speed * 0.35f;
            speed -= drop;
            comp_set_speeds(speed - steer, speed + steer);
        }
        break;
    }

    case COMP_PRIM_LINE_UNTIL_MARKER: {
        uint8_t gray = Get_Gray_Digital();
        uint8_t black_bits = comp_count_black_bits(gray);
        bool marker_present = black_bits >= COMP_MARKER_MIN_BLACK_BITS;
        bool marker_prestop;
        int32_t lap_avg_pulses = 0;
        float line_err;
        float steer;
        float speed;
        float drop;

        if (first_entry) {
            PID_SetTarget(&pidLine, 0.0f);
        }
        if (black_bits > g_competition.lap_marker_max_black_bits) {
            g_competition.lap_marker_max_black_bits = black_bits;
        }
        /* 只允许在本圈末端触发“横线首沿停车”。其目的不是替代A点判定，
         * 而是在灰度模块刚触及横线时锁住当前坐标，使整车不再越过横线。 */
        Competition_GetLapTravelPulses(0, 0, &lap_avg_pulses);
        /* 指定脉冲先停：近期42/38档回A平均约41942脉冲，提前3cm锁位。
         * 停车后再以当前灰度判断是否实际压在横线；不能等横线读数才决定制动。 */
        marker_prestop = s_marker_left_start &&
                         (g_competition.selected_mission == 0U) &&
                         lap_avg_pulses >= H1_MARKER_PRESTOP_MIN_PULSES;

        /* 启动时传感器可能正压在A点横线：必须先连续看到非全宽黑线，才允许回A触发停车。 */
        if (!s_marker_left_start) {
            if (!marker_present && seg_elapsed >= 100U) {
                s_marker_left_start = true;
                s_marker_confirm_samples = 0U;
                printf("H1: start marker cleared.\r\n");
            } else if (seg_elapsed > COMP_MARKER_ARM_TIMEOUT_MS) {
                printf("H1: cannot clear start marker.\r\n");
                Competition_SetFailed();
                break;
            }
        } else if (marker_prestop) {
            /* 指定脉冲优先停车：不再等横线才触发制动。停车瞬间只读一次灰度复核，
             * 只有确实压到横线（>=4黑）才单声提示；未压横线不鸣叫，供现场微调提前量。 */
            comp_capture_h1_result();
            g_competition.h1_result = H1_RUN_A_MARKER;
            comp_print_lap_travel("A-marker position stop");
            comp_h1_stop_now();
            s_t1_retreat_pending = true;
            if (black_bits >= H1_MARKER_PRESTOP_BLACK_BITS) {
                App_Indicator_RequestBeepOnce();
                printf("H1: position stop on A marker (%u black).\r\n", (unsigned)black_bits);
            } else {
                printf("H1: position stop, marker not covered (%u black).\r\n", (unsigned)black_bits);
            }
            comp_seg_advance();
            break;
        } else if (marker_present) {
            /* 未进入末端预停窗口时，仍以两帧6路黑作为安全回退确认。 */
            if (s_marker_confirm_samples < COMP_MARKER_CONFIRM_SAMPLES) {
                s_marker_confirm_samples++;
            }
            if (s_marker_confirm_samples >= COMP_MARKER_CONFIRM_SAMPLES) {
                comp_capture_h1_result();
                g_competition.h1_result = H1_RUN_A_MARKER;
                comp_print_lap_travel("A-marker");
                comp_h1_stop_now();
                /* 这是横线回退确认，不是“指定脉冲够”的完成路径，保持原停车行为。 */
                App_Indicator_RequestBeepOnce();
                printf("H1: return marker confirmed (2 samples); brake and hold armed.\r\n");
                comp_seg_advance();
                break;
            }
        } else {
            s_marker_confirm_samples = 0U;
        }

        /* 横线上的灰度位型不代表赛道中心。候选确认的两帧内不继续用其纠偏，
         * 以低速直行通过横线；若持续满足判据将直接进入STOP，若只是误触发则下一周期恢复循迹。 */
        if (s_marker_left_start && marker_present) {
            PID_Reset(&pidLine);
            s_h1_steer_limited = 0.0f;
            s_line_debug_error = 0.0f;
            s_line_debug_steer = 0.0f;
            s_line_debug_base_speed = H1_MARKER_CROSS_SPEED_CM_S;
            s_line_debug_gray = gray;
            s_line_debug_valid = 1U;
            comp_set_speeds(H1_MARKER_CROSS_SPEED_CM_S, H1_MARKER_CROSS_SPEED_CM_S);
            break;
        }

        line_err = -CalculateNormalizedValue(&g_GraySensor, 0U);
        /* 弧顶相邻灰度位型会给出方向相反的离群误差；先做3样本中值，
         * 再做限跃变低通，避免单帧错误进入增量式PID。 */
        s_h1_line_error_history[2] = s_h1_line_error_history[1];
        s_h1_line_error_history[1] = s_h1_line_error_history[0];
        s_h1_line_error_history[0] = line_err;
        if (s_h1_line_error_count < 3U) s_h1_line_error_count++;
        if (s_h1_line_error_count >= 3U) {
            line_err = comp_median3(s_h1_line_error_history[0],
                                    s_h1_line_error_history[1],
                                    s_h1_line_error_history[2]);
        }
        if (!s_h1_line_filter_valid) {
            s_h1_line_error_filtered = line_err;
            s_h1_line_filter_valid = true;
        } else {
            float delta = line_err - s_h1_line_error_filtered;
            delta = comp_clampf(delta, -H1_LINE_ERROR_STEP_LIMIT, H1_LINE_ERROR_STEP_LIMIT);
            s_h1_line_error_filtered += H1_LINE_ERROR_FILTER_ALPHA * delta;
        }
        line_err = s_h1_line_error_filtered;

        /* 弯道仅由当前实时误差滞回切换：离开弧线、回到中心探头后立即回到原直线PID，
         * 不保留任何按距离锁存的转向状态，避免弧线控制泄漏到直线。 */
        {
            float abs_err = fabsf(line_err);
            float enter = s_seg_node.curve_error_trigger;
            float exit = enter * 0.70f;
            if (!s_h1_curve_mode && abs_err >= enter) {
                s_h1_curve_mode = true;
            } else if (s_h1_curve_mode && abs_err <= exit) {
                s_h1_curve_mode = false;
            }
            if (s_h1_curve_mode) {
                pidLine.kp = s_seg_node.curve_kp;
                pidLine.kd = s_seg_node.curve_kd;
                pidLine.output_limit_p = s_seg_node.curve_max_steer;
                pidLine.output_limit_n = -s_seg_node.curve_max_steer;
            } else {
                pidLine.kp = s_seg_node.line_kp;
                pidLine.kd = s_seg_node.line_kd;
                pidLine.output_limit_p = s_seg_node.line_max_steer;
                pidLine.output_limit_n = -s_seg_node.line_max_steer;
            }
        }
        /* 只在曲线PID档把等效线误差向正侧偏置。根据当前符号，正误差输入会产生负steer
         * （左轮更快、右轮更慢），从而把车身小步向右修正。遥测仍保留真实滤波误差，
         * 便于确认S3/S4是否实际向S4/S5移动。 */
        steer = PID_Calc(&pidLine, line_err + (s_h1_curve_mode ? H1_CURVE_ERROR_BIAS : 0.0f));
        /* PID仍为增量式；即使输入已滤波，也不能让单个控制周期把左右差速反向翻转。 */
        steer = comp_clampf(steer, s_h1_steer_limited - H1_STEER_STEP_LIMIT,
                            s_h1_steer_limited + H1_STEER_STEP_LIMIT);
        s_h1_steer_limited = steer;
        /* T1空载15秒档弧线保留42→38cm/s降速；T4/T5滚球底盘基线要求全程恒基准速度，
         * 弧线不再被空载档强制提到38cm/s。 */
        speed = s_h1_curve_mode && g_competition.selected_mission == 0U
            ? H1_CURVE_SPEED_CM_S : seg->speed;
        speed = comp_ball_lap_speed(speed);
        speed = comp_ball_smooth_start_speed(speed);
        /* 快照必须来自同一控制周期：CSV中的灰度、误差和转向才能一一对应。 */
        s_line_debug_error = line_err;
        s_line_debug_steer = steer;
        s_line_debug_base_speed = speed;
        s_line_debug_gray = gray;
        s_line_debug_valid = 1U;
        drop = fabsf(steer) * 0.15f;
        if (drop > speed * 0.35f) drop = speed * 0.35f;
        speed -= drop;
        comp_set_speeds(speed - steer, speed + steer);
        break;
    }

    default:
        /* 未知原语: 跳过 */
        comp_seg_advance();
        break;
    }
}

/* ==================== 主调度 ==================== */

void Competition_Task(void)
{
    bool ball_task_pass;

    /* T2 正常完成后仍需在 10ms 周期内闭环保持底盘当前位置。
     * 计时已经冻结，滚球状态也保持 COMPLETE_PASS/-5cm；这里只更新双轮
     * 位置保持输出，直到 K2 停止、任务切换或重新启动显式释放。 */
    if (g_competition.state == COMP_STATE_FINISHED) {
        if ((Competition_GetBallMode() == COMP_BALL_MODE_STATIC_SEQUENCE) &&
            s_h1_hold_active && s_h1_hold_persistent) {
            (void)comp_hold_task();
        }
        return;
    }

    if (g_competition.state != COMP_STATE_RUNNING) return;

    g_competition.task_elapsed_ms = mspm0_get_clock_ms() - g_competition.task_start_ms;

    /* T1达到指定脉冲后的收尾：必须先完成短时锁位，确认刹停后才允许后退。
     * 这样避免高速循线结束时直接反向，减少电机与车身冲击。 */
    if (s_t1_retreat_pending) {
        if (!comp_hold_task()) return;
        s_t1_retreat_pending = false;
        s_t1_retreat_start_encL = encoderLeft.total_count;
        s_t1_retreat_start_encR = encoderRight.total_count;
        s_t1_retreat_start_ms = mspm0_get_clock_ms();
        s_t1_retreat_active = true;
        printf("H1 T1: stopped; retreating 2cm.\r\n");
        return;
    }

    if (s_t1_retreat_active) {
        (void)comp_t1_retreat_task();
        return;
    }

    if (s_t2_retreat_active) {
        if (comp_t2_retreat_task()) {
            g_competition.state = COMP_STATE_FINISHED;
            s_line_debug_valid = 0U;
            s_line_debug_base_speed = 0.0f;
            App_Indicator_RequestLongBeep();
            App_Indicator_SetMissionRunning(false);
            App_Indicator_SetRedBlink(false);
            g_imu_enabled = true;
            printf("=== Ball sequence complete; retreated 2cm and position hold active ===\r\n");
        }
        return;
    }

    if (Competition_GetBallMode() == COMP_BALL_MODE_STATIC_SEQUENCE) {
        if (BallTask3_IsComplete(&ball_task_pass)) {
            if (ball_task_pass) {
                /* T2 的 -5cm 球终点确认后，先解除原地锁位并以编码器闭环
                 * 缓慢后退2cm；后退结束再重新电刹锁位，避免立即刹停扰动滚珠。 */
                comp_hold_cancel();
                s_t2_retreat_start_encL = encoderLeft.total_count;
                s_t2_retreat_start_encR = encoderRight.total_count;
                s_t2_retreat_active = true;
                printf("Ball sequence complete; retreating chassis 2cm before final hold.\r\n");
            } else {
                Competition_SetFailed();
            }
            return;
        }
    }

    CompMission_t *m = &s_missions[g_competition.selected_mission];
    if (!m->used || m->seg_count == 0) {
        Competition_Stop();
        return;
    }

    exec_segment(m);
}
