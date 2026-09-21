/**
 * @file      car.c
 * @brief     底盘运动控制 (编码器转弯版，不需要陀螺仪)
 * @details   路口检测 → 编码器差速弧线转弯 → 寻线 → 循迹
 *            90度转弯靠编码器差值: W×π/2 = 12×1.5708 ≈ 18.85cm ≈ 1344脉冲
 */

#include "car.h"
#include "encoder_driver.h"
#include "No_Mcu_Ganv_Grayscale_Sensor.h"
#include "pid.h"
#include "app_indicator.h"
#include "sys_time.h"
#include <stdlib.h>

/* ------------------ 外部硬件和变量 ------------------ */
extern Encoder_t encoderLeft;
extern Encoder_t encoderRight;
extern PID_t pidLine;
extern int run_mode;
extern float targetSpeedLeft;
extern float targetSpeedRight;
extern float current_base_speed;
extern float base_speed;
extern float steer;
extern No_MCU_Sensor g_GraySensor;

/* ------------------ 内部函数声明 ------------------ */
uint8_t Get_Gray_Digital(void);
LineType_e DetectIntersectionType(uint8_t digital);
LineType_e GetStableLineType(void);
void ResetLineDetection(void);

/* ------------------ 转弯参数 ------------------ */
#define TURN_INNER_SPEED  10.0f    // 内轮速度 cm/s
#define TURN_OUTER_SPEED  25.0f    // 外轮速度 cm/s
#define TURN_TARGET_DIFF  1344     // 90度转弯编码器差值 (脉冲)
#define TURN_BLIND_DIFF   300      // 盲转阶段差值 (不查线)
#define TURN_MAX_DIFF     1800     // 最大差值 (超时保护)
#define CORNER_COOLDOWN   500      // 转弯后冷却距离(脉冲),防止重复触发

/* ------------------ 任务控制参数 ------------------ */
#define RETURN_STOP_COMPENSATION 420

LineFollowControl_t g_MissionControl = {
    .target_laps = 1,
    .current_laps = 0,
    .current_lap_turns = 0,
    .is_running = false,
    .first_turn_detected = false,
    .second_turn_detected = false,
    .all_laps_completed = false,
    .turn_direction = 1
};

ChassisState_e chassis_state = STATE_STOP;
TurnControl_t g_turnControl = {0, TURN_PHASE_NONE, 0};

/* 转弯起始编码器 */
static int32_t turn_start_left;
static int32_t turn_start_right;
/* 转弯结束后冷却: 走够距离才允许检测下一个路口 */
static int32_t cooldown_start_left;
static bool    corner_cooldown = false;

/* ------------------ 核心逻辑实现 ------------------ */

int CarInit(void) { return 0; }

uint8_t Get_Gray_Digital(void)
{
    return ~Get_Digtal_For_User(&g_GraySensor);
}

LineType_e DetectIntersectionType(uint8_t digital)
{
    int left_cnt = 0, right_cnt = 0;
    int i;
    for (i = 0; i < 4; i++)
    {
        if (digital & (1 << (i + 4))) left_cnt++;
        if (digital & (1 << i))       right_cnt++;
    }
    int total = left_cnt + right_cnt;

    if (total == 0)           return LINE_LOST;
    if (total >= 7)           return LINE_CROSS;

    /* 5-6路全黑: 哪侧多就往哪侧转 (直角处直行+转弯线重叠) */
    if (total >= 5)
    {
        if (left_cnt > right_cnt)  return LINE_LEFT;
        if (right_cnt > left_cnt)  return LINE_RIGHT;
        return LINE_CROSS;  /* 3+3 平分 */
    }

    /* 4路以下: 严格判断 */
    if (left_cnt >= 4)        return LINE_T_LEFT;
    if (right_cnt >= 4)       return LINE_T_RIGHT;
    if (left_cnt >= 3 && right_cnt <= 1) return LINE_LEFT;
    if (right_cnt >= 3 && left_cnt <= 1) return LINE_RIGHT;
    return LINE_STRAIGHT;
}

/* 路口检测内部状态 (文件级, 可被 ResetLineDetection 清零) */
static LineType_e s_last_detected = LINE_NONE;
static LineType_e s_stable_detected = LINE_NONE;
static int s_debounce_counter = 0;

LineType_e GetStableLineType(void)
{
    uint8_t digital = Get_Gray_Digital();
    LineType_e current = DetectIntersectionType(digital);

    if (current == s_last_detected) {
        s_debounce_counter++;
    } else {
        s_debounce_counter = 1;
        s_last_detected = current;
    }
    if (s_debounce_counter >= 1)
        s_stable_detected = current;
    return s_stable_detected;
}

/* 重置路口检测的内部状态 (K2/K3 重启时调用) */
void ResetLineDetection(void)
{
    s_last_detected = LINE_NONE;
    s_stable_detected = LINE_NONE;
    s_debounce_counter = 0;
}

void LineFollow_SetTargetLaps(int laps)
{
    if (laps >= 1 && laps <= 10) {
        g_MissionControl.target_laps = laps;
        App_Indicator_RequestBeepCount((uint8_t)laps);
    }
}

void Car_MissionStart(void)
{
    /* 清理上一次运行的所有状态 */
    g_MissionControl.current_laps = 0;
    g_MissionControl.current_lap_turns = 0;
    g_MissionControl.first_turn_detected = false;
    g_MissionControl.second_turn_detected = false;
    g_MissionControl.all_laps_completed = false;
    g_MissionControl.side_length_pulses = 7130;

    /* 重置转弯相关状态 */
    corner_cooldown = false;
    g_turnControl.phase = TURN_PHASE_NONE;
    turn_start_left = 0;
    turn_start_right = 0;

    /* 重置路口检测去抖 */
    ResetLineDetection();

    /* 重置 PID */
    PID_Reset(&pidLine);
    steer = 0.0f;

    /* 重置速度 */
    current_base_speed = 10.0f;  /* 起步速度, 不从0开始 */

    g_MissionControl.is_running = true;
    g_MissionControl.start_encoder_left = encoderLeft.total_count;
    App_Indicator_SetMissionRunning(true);

    chassis_state = STATE_LINE_FOLLOW;
    run_mode = 1;
}

void Car_MissionStop(void)
{
    g_MissionControl.is_running = false;
    chassis_state = STATE_STOP;
    run_mode = 0;
    corner_cooldown = false;
    g_turnControl.phase = TURN_PHASE_NONE;
    App_Indicator_SetMissionRunning(false);
}

/* 计算转弯编码器差值 */
static int32_t GetTurnDiff(int direction)
{
    int32_t dl = encoderLeft.total_count - turn_start_left;
    int32_t dr = encoderRight.total_count - turn_start_right;
    if (direction == 1)  // 左转: 右轮(外)走得多
        return dr - dl;
    else                  // 右转: 左轮(外)走得多
        return dl - dr;
}

/* 启动编码器转弯 */
static void StartEncoderTurn(int direction)
{
    turn_start_left  = encoderLeft.total_count;
    turn_start_right = encoderRight.total_count;
    current_base_speed = base_speed;

    if (direction == 1) {  // 左转
        targetSpeedLeft  = TURN_INNER_SPEED;
        targetSpeedRight = TURN_OUTER_SPEED;
    } else {                // 右转
        targetSpeedLeft  = TURN_OUTER_SPEED;
        targetSpeedRight = TURN_INNER_SPEED;
    }

    g_turnControl.phase = TURN_PHASE_BLIND;
    run_mode = 3;  // 编码器转弯模式
}

/* 检查转弯是否完成 */
static int TurnComplete(int direction)
{
    int32_t diff = GetTurnDiff(direction);

    /* 超时保护 */
    if (diff > TURN_MAX_DIFF)
        return 1;

    /* 盲转阶段: 差值不够不查线 */
    if (g_turnControl.phase == TURN_PHASE_BLIND)
    {
        if (diff >= TURN_BLIND_DIFF)
            g_turnControl.phase = TURN_PHASE_SEARCH;
        return 0;
    }

    /* 寻线阶段: 差值够了或找到线 */
    if (diff >= TURN_TARGET_DIFF)
        return 1;

    uint8_t digital = Get_Gray_Digital();
    if (digital & 0x7E)  // 找到线了
        return 1;

    return 0;
}

void CarTask(void)
{
    if (!g_MissionControl.is_running)
        return;

    LineType_e line_type = GetStableLineType();

    switch (chassis_state)
    {
        case STATE_LINE_FOLLOW:
            run_mode = 1;

            /* 冷却距离到了 → 解除冷却 */
            if (corner_cooldown &&
                abs(encoderLeft.total_count - cooldown_start_left) >= CORNER_COOLDOWN)
            {
                corner_cooldown = false;
            }

            /* 所有圈数完成 → 回起点停车 */
            if (g_MissionControl.all_laps_completed)
            {
                int32_t current_dist = abs(encoderLeft.total_count - g_MissionControl.last_turn_encoder_left);
                int32_t target_dist = g_MissionControl.side_length_pulses -
                                      g_MissionControl.start_to_first_turn_pulses -
                                      RETURN_STOP_COMPENSATION;
                if (target_dist < 0) target_dist = 0;
                if (current_dist >= target_dist) {
                    Car_MissionStop();
                    break;
                }
            }
            /* 检测到路口 → 编码器转弯 (冷却期内不检测) */
            else if (!corner_cooldown &&
                     (line_type == LINE_LEFT  || line_type == LINE_T_LEFT ||
                      line_type == LINE_RIGHT || line_type == LINE_T_RIGHT ||
                      line_type == LINE_CROSS))
            {
                if (!g_MissionControl.first_turn_detected)
                {
                    g_MissionControl.start_to_first_turn_pulses =
                        abs(encoderLeft.total_count - g_MissionControl.start_encoder_left);
                    g_MissionControl.first_turn_detected = true;

                    if (line_type == LINE_LEFT || line_type == LINE_T_LEFT)
                        g_MissionControl.turn_direction = 1;
                    else
                        g_MissionControl.turn_direction = 2;
                }
                else if (!g_MissionControl.second_turn_detected)
                {
                    g_MissionControl.side_length_pulses =
                        abs(encoderLeft.total_count - g_MissionControl.last_turn_encoder_left);
                    g_MissionControl.second_turn_detected = true;
                }

                chassis_state = (g_MissionControl.turn_direction == 1)
                              ? STATE_LEFT_TURN : STATE_RIGHT_TURN;

                StartEncoderTurn(g_MissionControl.turn_direction);
            }
            break;

        case STATE_LEFT_TURN:
        case STATE_RIGHT_TURN:
            if (TurnComplete(g_MissionControl.turn_direction))
            {
                PID_Reset(&pidLine);
                steer = 0.0f;  /* 清除转向残留, 防止直线偏移 */
                chassis_state = STATE_LINE_FOLLOW;
                run_mode = 1;

                /* 启动冷却: 走够 500 脉冲后才允许检测下一个路口 */
                corner_cooldown = true;
                cooldown_start_left = encoderLeft.total_count;

                g_MissionControl.last_turn_encoder_left = encoderLeft.total_count;
                g_MissionControl.current_lap_turns++;
                if (g_MissionControl.current_lap_turns >= 4) {
                    g_MissionControl.current_lap_turns = 0;
                    g_MissionControl.current_laps++;
                }
                if (g_MissionControl.current_laps >= g_MissionControl.target_laps) {
                    g_MissionControl.all_laps_completed = true;
                }
            }
            break;

        case STATE_STOP:
            run_mode = 0;
            break;

        default:
            break;
    }
}