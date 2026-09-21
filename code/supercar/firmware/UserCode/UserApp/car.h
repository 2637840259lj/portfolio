/**
 * @file      car.h
 * @brief     底盘运动控制核心头文件
 * @details   包含底盘状态定义、循迹绕圈控制结构体以及运动控制接口
 * @author    PJ_Hr
 * @version   V1.1
 * @date      2026/03/18
 * @copyright pjh
 */

#ifndef _CAR_H
#define _CAR_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 底盘运动主状态枚举
 */
typedef enum
{
    STATE_LINE_FOLLOW = 0, // 循迹模式
    STATE_LEFT_TURN,        // IMU 角度闭环左转
    STATE_RIGHT_TURN,       // IMU 角度闭环右转
    STATE_STOP              // 停止模式 (主动刹车)
} ChassisState_e;

/**
 * @brief IMU 角度转弯阶段枚举
 */
typedef enum
{
    TURN_PHASE_NONE = 0,
    TURN_PHASE_BLIND,   // 盲转阶段：纯 IMU 角度闭环，不检测线
    TURN_PHASE_SEARCH    // 寻线阶段：等待灰度传感器重新找到线
} TurnPhase_e;

/**
 * @brief IMU 转弯控制结构体
 */
typedef struct
{
    uint32_t startTick;     // 转弯起始时间 (ms)
    TurnPhase_e phase;      // 当前转弯阶段
    float     start_yaw;    // 转弯起始 yaw 角 (预留)
} TurnControl_t;

/**
 * @brief 灰度传感器识别的路口类型
 */
typedef enum
{
    LINE_NONE = 0,
    LINE_STRAIGHT,
    LINE_LEFT,
    LINE_RIGHT,
    LINE_CROSS,
    LINE_T_LEFT,
    LINE_T_RIGHT,
    LINE_LOST
} LineType_e;

/**
 * @brief 循迹绕圈任务控制结构体
 */
typedef struct
{
    volatile int target_laps;       // 目标圈数 (Key1 设定)
    volatile int current_laps;      // 当前已完成圈数
    volatile int current_lap_turns; // 当前圈已完成的转弯次数 (4次/圈)

    volatile bool is_running;           // 任务运行标志
    volatile bool first_turn_detected;  // 是否已检测到第一个路口 (用于记录起点偏移)
    volatile bool second_turn_detected; // 是否已检测到第二个路口 (用于测量边长)
    volatile bool all_laps_completed;   // 所有设定圈数是否已完成

    int32_t start_encoder_left;         // 任务启动时的左轮总脉冲数
    int32_t start_to_first_turn_pulses; // 起点到第一个路口的探测距离 (脉冲)
    int32_t side_length_pulses;         // 正方形一条边的完整脉冲数
    int32_t last_turn_encoder_left;     // 最后一次转弯结束时的左轮脉冲数

    int turn_direction; // 绕圈方向基准: 1-左转绕圈(逆时针), 2-右转绕圈(顺时针)
} LineFollowControl_t;

/* ------------------ 全局变量外部声明 ------------------ */
extern LineFollowControl_t g_MissionControl;
extern ChassisState_e chassis_state;
extern TurnControl_t g_turnControl;

/* ------------------ 运动控制接口函数 ------------------ */

/**
 * @brief 初始化底盘硬件、PID参数及传感器
 * @return int 0: 成功
 */
int CarInit(void);

/**
 * @brief 底盘任务主循环 (需在 main while(1) 中调用)
 * @details 处理状态机切换、路口计数及终点判断
 */
void CarTask(void);

/**
 * @brief 设定绕圈任务的目标圈数
 * @param laps 目标圈数 (建议 1-10)
 */
void LineFollow_SetTargetLaps(int laps);

/**
 * @brief 启动绕圈任务
 * @details 重置所有计数器，记录起点，切换至循迹状态
 */
void Car_MissionStart(void);

/**
 * @brief 停止底盘运动
 * @details 停止任务运行并执行主动刹车
 */
void Car_MissionStop(void);

#endif //_CAR_H
