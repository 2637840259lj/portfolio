#ifndef __APP_TEACH_H
#define __APP_TEACH_H

/**
 * @file    app_teach.h
 * @brief   示教编程 — 航点录制与回放
 *
 * 航点格式 (14 字节):
 *   [encL:i32] [encR:i32] [speed:u8] [action:u8] [timeout_ms:u16]
 *
 * Action 码:
 *   0x00 MOVE_TO  - 编码器位置走
 *   0x01 TURN_L90 - 原地左转 90°
 *   0x02 TURN_R90 - 原地右转 90°
 *   0x03 WAIT     - 延时
 *   0x04 LINE_TO  - 循线段
 *   0x05 END      - 序列结束
 */

#include <stdint.h>
#include <stdbool.h>

#define MAX_WAYPOINTS  100
#define MAX_SEQUENCES  4

typedef struct {
    int32_t  encL;
    int32_t  encR;
    uint8_t  speed;
    uint8_t  action;
    uint16_t timeout_ms;
} waypoint_t;

/* ==================== API ==================== */

void AppTeach_Init(void);

/* 录制 */
void AppTeach_StartRecording(uint8_t rate_hz);
void AppTeach_StopRecording(void);
void AppTeach_RecordWaypoint(void);

/* 回放 */
void AppTeach_StartPlayback(uint8_t speed_scale);
void AppTeach_Pause(void);
void AppTeach_Resume(void);
void AppTeach_Abort(void);

/* 航点管理 */
bool AppTeach_AddWaypoint(const waypoint_t *wp);
void AppTeach_ClearSequence(void);
uint8_t AppTeach_GetWaypointCount(void);

/* 序列列表 (手机 App 查询) */
uint8_t AppTeach_ListSequences(char names[4][16]);

/**
 * 主循环 10ms 调用: 回放状态机 + 录制定时采样。
 * 返回 true 表示回放进行中 (主循环不要写入速度目标)。
 */
bool AppTeach_PlaybackTask(void);

/** 录制中或回放/暂停中 → true (仲裁与 EMG 用) */
bool AppTeach_IsActive(void);

/**
 * 获取回放进度: 0~100 (%)
 */
uint8_t AppTeach_GetProgress(void);

#endif /* __APP_TEACH_H */
