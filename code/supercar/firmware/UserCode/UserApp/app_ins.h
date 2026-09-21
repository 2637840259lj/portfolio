#ifndef __APP_INS_H
#define __APP_INS_H

/**
 * @file    app_ins.h
 * @brief   指令示教脚本解释器
 *
 * 指令格式: [opcode:u8][p1:i16][p2:i16][p3:i16][p4:i16] = 9 bytes (wire)
 * 注意: sizeof(ins_t) = 10 bytes (AAPCS 下 uint8_t + int16_t 有 1 字节填充)
 * 缓冲区: 250 条, 约 2500 字节
 * 执行: 10ms 主循环调用 AppIns_Task()
 *
 * opcode 速查 (运动类已接入全局 PID + schedule 插值):
 *   0x01 MOVE_CM(p1=dist_cm*10, p2=speed)      位置闭环直行 + 航向保持
 *   0x02 MOVE_RAMP(p1=dist_cm*10)              位置闭环直行 (base_speed)
 *   0x03 MOVE_TRANSITION(p1=dist_cm*10)        位置闭环直行, 到位不停
 *   0x04 MOVE_IMU(p1=dist_cm*10, p2=speed)     航向 PID + 位置减速 (需 IMU)
 *   0x05 MOVE_TIME(p1=time_ms, p2=spdL/10, p3=spdR/10)  定时直行 (开环)
 *   0x10 ROTATE_INPLACE_IMU(p1=deg*10, p2=speed)        航向闭环原地转 (需 IMU)
 *   0x11 ROTATE_INPLACE_ENC(p1=deg*10, p2=speed)        编码器原地转 (开环)
 *   0x20 LINE_FOLLOW(p1=dist_cm*10, p2=speed)           循线 PID + 位置减速
 *   0x21 LINE_UNTIL_LOST(p1=timeout_ms, p2=speed)       循线直到丢线
 *   0x22 LINE_FOLLOW_TIME(p1=time_ms, p2=speed)         定时长循线
 *   0x23 LINE_CURVE(p1=dist_cm*10, p2=speed)            弧线循线 (curve_kp/kd)
 *   0x30 WAIT_BLACK(p1=timeout_ms)            等待检测到黑线
 *   0x31 WAIT_NOBLACK(p1=timeout_ms)          等待离开黑线
 *   0x33 STOP_IF_BLACK                        检测到黑线则停车
 *   0x40 SET_BASE_SPEED(p1=speed)             设置 base_speed 参数
 *   0x41 SET_SPEED_RAW(p1=spdL/10, p2=spdR/10, p3=time_ms)  裸速度+定时
 *   0x52 YAW_RESET                            重置航向基准
 *   0x53 ENC_RESET                            重置编码器基准
 *   0x54 DELAY(p1=time_ms)                    延时等待
 *   0x56 BRAKE                                刹车
 *   0x60 LABEL(p1=id)                         标签定义
 *   0x61 JUMP(p1=label_id)                    无条件跳转
 *   0x62 JUMP_IF(p1=label, p2=cond, p3=val)   条件跳转
 *   0x63 IF(p1=cond, p2=val) ... 0x64 ELSE ... 0x65 END_IF   条件分支
 *   0x66 FOR(p1=var, p2=start, p3=end, p4=step) ... 0x67 END_FOR   循环
 *   0x68 WHILE(p1=cond, p2=val) ... 0x69 END_WHILE            条件循环
 *   0x6A BREAK / 0x6B CONTINUE               循环控制
 *   0x6F END_SCRIPT                          脚本结束
 *
 * 运动指令安全保护:
 *   - 超时: 基于距离/速度估算, 3 倍裕度, 限制 3~30 秒 (旋转 3~15 秒)
 *   - 编码器卡死: 连续 1 秒距离不增长 → 报错停车
 *   - IMU 失联: 0x04/0x10 启动时检查, 失联 → 报错停车
 *   - 错误码通过 BLE_CMD_INS_EVENT 发送: 0xFF=timeout 0xFE=encoder 0xFD=imu
 */

#include <stdint.h>
#include <stdbool.h>

#define INS_BUF_MAX    250
#define LABEL_MAX      32
#define NEST_MAX       8
#define LOOP_VAR_COUNT 4

/* 指令结构体 (9 bytes, 与协议包中的 INS_APPEND payload 一致) */
typedef struct {
    uint8_t  opcode;
    int16_t  p1, p2, p3, p4;
} ins_t;

/* 执行阶段 */
typedef enum {
    INS_EXEC_IDLE = 0,   /* 空闲, 未执行 */
    INS_EXEC_RUNNING,    /* 正在执行 (等待运动完成) */
    INS_EXEC_WAITING,    /* 等待中 (delay/传感器) */
    INS_EXEC_DONE,       /* 当前指令执行完毕, 可移至下一条 */
    INS_EXEC_ERROR       /* 运行时错误 */
} ins_exec_state_t;

/* ==================== API ==================== */

void AppIns_Init(void);

/* 指令缓冲区管理 */
void    AppIns_Clear(void);
bool    AppIns_Append(const ins_t *ins);
bool    AppIns_Insert(uint8_t index, const ins_t *ins);
bool    AppIns_Delete(uint8_t index);
uint16_t AppIns_GetCount(void);
uint16_t AppIns_GetIP(void);

/* 读取第 index 条指令 (0-based), 返回 false 表示越界 */
bool AppIns_GetAt(uint8_t index, ins_t *out);

/* 执行控制 */
void AppIns_Exec(void);
void AppIns_Stop(void);
void AppIns_Pause(void);
void AppIns_Resume(void);
void AppIns_Step(void);

/* 主循环 10ms 调用: 解释器核心 */
void AppIns_Task(void);

/* 返回 true 表示脚本正在执行 (供运动仲裁判断) */
bool AppIns_IsActive(void);

#endif /* __APP_INS_H */
