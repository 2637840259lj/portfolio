/**
 * @file      app_template.h
 * @brief     通用底层闭环台架测试（不包含竞赛状态机）
 */
#ifndef __APP_TEMPLATE_H__
#define __APP_TEMPLATE_H__

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    APP_BENCH_NONE = 0,
    APP_BENCH_LINE_STRAIGHT = 1,
    APP_BENCH_YAW90 = 2,
    APP_BENCH_DISTANCE_1M = 3,
    APP_BENCH_LINE_CURVE = 4
} app_bench_mode_t;

/* 兼容旧的通用循迹启动命令：其语义固定为直线循迹。 */
#define APP_BENCH_LINE APP_BENCH_LINE_STRAIGHT

void AppTemplate_Init(void);
void AppTemplate_Task(void);

/* 仅允许在小车静止、无遥控控制权时启动。index 对应 20/30/40 cm/s 曲线节点。 */
bool AppBench_Start(app_bench_mode_t mode, uint8_t node_index);
void AppBench_Task(void);       /* 在 10ms 控制周期中调用 */
void AppBench_Abort(void);      /* STOP/BRK/EMG 和其他独占任务统一调用 */
bool AppBench_IsActive(void);

#endif
