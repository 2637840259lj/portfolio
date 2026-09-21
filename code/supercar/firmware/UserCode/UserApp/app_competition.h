/**
 * @file      app_competition.h
 * @brief     通用竞赛任务框架 — 段表驱动 (2026 赛题适配层)
 *
 * @details
 *   设计目标:
 *   - 不含任何硬编码赛题任务; 赛题路线以 CompSegment_t 段表描述, 运行时注册。
 *   - 原语复用全局 PID (pidLine / pidAngle / pidPosition) + 三节点 schedule 插值,
 *     与 app_template 台架、app_ins 指令示教共用同一套速度内环硬底座。
 *   - 保留旧 API 名 (Init/Start/Stop/PrevTask/NextTask/GetState) 兼容按键与 BLE。
 *
 *   段表原语:
 *     LINE_FOLLOW   — 灰度循线 + 位置减速
 *     YAW_HOLD      — IMU 航向保持直行 + 位置减速
 *     DRIVE_DISTANCE— 双轮编码器定距直行 (无循线, 可无 IMU)
 *     TURN_YAW      — 原地/行进中转到目标航向
 *     WAIT          — 停车等待
 *     STOP          — 停车 (序列结束)
 *     LINE_UNTIL_LOST — 循线直到丢线 (走完弧线/出口)
 *     LINE_UNTIL_MARKER — 先离开起点横线，再循线至下一次全宽横线
 *
 *   使用流程:
 *     1. Competition_Init()          — 注册内置 debug mission
 *     2. Competition_RegisterMission — 注册赛题 mission (题目确定后)
 *     3. Competition_AppendSegment   — 向 mission 追加段 (或 BLE 编辑)
 *     4. Competition_PrevTask/NextTask — 在 mission 列表里切换
 *     5. Competition_Start           — 启动当前 mission
 *     6. Competition_Task (10ms)     — 段表回放, 直接写 targetSpeedLeft/Right
 */
#ifndef __APP_COMPETITION_H__
#define __APP_COMPETITION_H__

#include <stdint.h>
#include <stdbool.h>

/* ==================== 原语类型 ==================== */
typedef enum {
    COMP_PRIM_NONE          = 0,
    COMP_PRIM_LINE_FOLLOW   = 1,  /* 灰度循线, 距离到位 */
    COMP_PRIM_YAW_HOLD      = 2,  /* IMU 航向保持直行, 距离到位 */
    COMP_PRIM_DRIVE_DISTANCE= 3,  /* 编码器定距直行 (无循线) */
    COMP_PRIM_TURN_YAW      = 4,  /* 转到目标航向 (delta_deg) */
    COMP_PRIM_WAIT          = 5,  /* 停车等待 */
    COMP_PRIM_STOP          = 6,  /* 停车 */
    COMP_PRIM_LINE_UNTIL_LOST = 7, /* 循线直到丢线 */
    COMP_PRIM_LINE_UNTIL_MARKER = 8, /* 离开起点横线后循线至下一次全宽横线 */
    COMP_PRIM_HOLD_POSITION = 9 /* 双轮编码器位置保持，直到按键安全停止 */
} CompPrimitive_e;

/* ==================== 滚球策略 ==================== */
typedef enum {
    COMP_BALL_MODE_OFF = 0,       /* 不要求滚球，仅底盘任务 */
    COMP_BALL_MODE_STATIC_SEQUENCE,/* 静止 O -> +5cm -> -5cm */
    COMP_BALL_MODE_MOTION_BALANCE  /* 行驶中保持滚珠在物理中心 */
} CompBallMode_e;

/* ==================== 段表条目 ==================== */
typedef struct {
    CompPrimitive_e prim;       /* 原语类型 */
    float    speed;             /* 目标速度 cm/s */
    int32_t  dist_pulses;       /* 目标距离 (编码器脉冲, 双轮平均) */
    float    yaw_delta;         /* TURN_YAW: 目标航向偏移 (度) */
    uint16_t timeout_ms;        /* 超时 (0 = 不限) */
    uint16_t wait_ms;           /* WAIT 原语等待时间 */
    uint8_t  node_index;        /* schedule 节点 (0=20/1=30/2=40 cm/s), 255=按 speed 插值 */
} CompSegment_t;

/* ==================== Mission ==================== */
#define COMP_MAX_SEGMENTS  24
#define COMP_MAX_MISSIONS  8

typedef struct {
    char           name[16];
    CompSegment_t  segments[COMP_MAX_SEGMENTS];
    uint8_t        seg_count;
    CompBallMode_e ball_mode;  /* 当前任务的滚珠策略 */
    bool           used;
} CompMission_t;

/* ==================== 系统状态 ==================== */
typedef enum {
    COMP_STATE_IDLE = 0,
    COMP_STATE_RUNNING,
    COMP_STATE_FINISHED,
    COMP_STATE_FAILED
} CompetitionState_e;

/* H题单圈结果：供App直接显示，避免依赖UART日志或手算CSV。 */
typedef enum {
    H1_RUN_IDLE = 0,
    H1_RUN_RUNNING,
    H1_RUN_A_MARKER,
    H1_RUN_TIMEOUT,
    H1_RUN_MANUAL_STOP,
    H1_RUN_FAILED
} H1RunResult_e;

/* 旧 API 兼容: task 编号即 mission 索引+1 */
typedef enum {
    COMP_TASK_NONE = 0,
    COMP_TASK_1 = 1,
    COMP_TASK_2 = 2,
    COMP_TASK_3 = 3,
    COMP_TASK_4 = 4,
    COMP_TASK_5 = 5,
    COMP_TASK_MAX
} CompetitionTask_e;

/* ==================== 全局状态 ==================== */
typedef struct {
    CompetitionState_e state;
    uint8_t  selected_mission;   /* 当前选中的 mission 索引 (0-based) */
    uint8_t  current_seg;        /* 当前执行的段索引 */
    uint32_t seg_start_ms;       /* 当前段起始时间 */
    int32_t  seg_start_encL;     /* 当前段起始编码器 */
    int32_t  seg_start_encR;
    float    seg_start_yaw;      /* 当前段起始航向 */
    uint32_t task_start_ms;      /* 任务起始时间 */
    uint32_t task_elapsed_ms;    /* 任务已用时间 */
    int32_t  task_start_encL;    /* 单圈起跑编码器基准 */
    int32_t  task_start_encR;
    int32_t  lap_finish_encL;    /* A点确认/结束瞬间的单圈行程快照 */
    int32_t  lap_finish_encR;
    uint8_t  lap_marker_max_black_bits; /* 本圈出现过的最大黑路数 */
    H1RunResult_e h1_result;
    bool     emergency_stop;
} CompetitionControl_t;

extern CompetitionControl_t g_competition;

/* ==================== API ==================== */

void Competition_Init(void);
void Competition_Task(void);   /* 10ms 主循环调用, 直接写 targetSpeedLeft/Right */

/* Mission 管理 */
uint8_t Competition_RegisterMission(const char *name);                    /* 返回索引, 255=满 */
bool    Competition_AppendSegment(uint8_t mission_idx, const CompSegment_t *seg);
bool    Competition_ClearMission(uint8_t mission_idx);
uint8_t Competition_GetMissionCount(void);
const char *Competition_GetMissionName(uint8_t mission_idx);

/* 旧 API 兼容 (按键/BLE) */
void Competition_SelectTask(CompetitionTask_e task);
void Competition_PrevTask(void);
void Competition_NextTask(void);
void Competition_Start(void);
void Competition_Stop(void);

CompetitionTask_e  Competition_GetSelectedTask(void);
CompetitionState_e Competition_GetState(void);
uint32_t           Competition_GetElapsedMs(void);
/* 当前任务编号/名称供OLED与按键任务选择界面显示。 */
uint8_t            Competition_GetSelectedMissionIndex(void);
CompBallMode_e     Competition_GetBallMode(void);
void               Competition_SetFailed(void);
/* 返回自本次起跑以来左右轮绝对行程及平均行程，单位为编码器脉冲。 */
void               Competition_GetLapTravelPulses(int32_t *left, int32_t *right,
                                                   int32_t *average);
/* H题本圈结果快照：状态、冻结用时、行程脉冲与横线最大黑路数。 */
void               Competition_GetH1ResultSnapshot(uint8_t *state, uint32_t *elapsed_ms,
                                                    int32_t *left_pulses, int32_t *right_pulses,
                                                    uint8_t *max_black_bits);

/* H题循迹外环遥测快照：由比赛任务每10ms刷新，BLE只读发送。
 * line_error / line_steer 均是未缩放的控制量；valid=1 说明当前正在执行循迹原语。 */
void Competition_GetLineDebugSnapshot(float *line_error, float *line_steer,
                                      float *base_speed, uint8_t *gray,
                                      uint8_t *valid);

#endif /* __APP_COMPETITION_H__ */
