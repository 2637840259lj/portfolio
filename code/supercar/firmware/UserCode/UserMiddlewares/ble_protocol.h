#ifndef __BLE_PROTOCOL_H
#define __BLE_PROTOCOL_H

/**
 * @file    ble_protocol.h
 * @brief   蓝牙二进制通信协议栈
 *
 * 帧格式:
 *   [0xAA] [LEN] [CMD] [PAYLOAD:0~250B] [XOR8]
 *
 *   LEN   = payload 字节数 (不含自身)
 *   XOR8  = LEN ^ CMD ^ PAYLOAD[0] ^ ... ^ PAYLOAD[N-1]
 *
 * 接收状态机从 ISR 喂字节，组完完整帧后置 ready 标志。
 * 主循环读 ready 标志并消费，再调用 BLE_Protocol_Done() 释放。
 */

#include <stdint.h>
#include <stdbool.h>

/* 命令码常量 */
#define BLE_CMD_PING          0x00
#define BLE_CMD_RESET         0x01
#define BLE_CMD_GET_STATE     0x02
#define BLE_CMD_ACK           0x03
#define BLE_CMD_NACK          0x04
#define BLE_CMD_GET_HW_STATUS 0x05
#define BLE_CMD_GET_PAGE_DATA 0x06

#define BLE_CMD_MOVE_RAW      0x10
#define BLE_CMD_STOP          0x11
#define BLE_CMD_EMERGENCY     0x12
#define BLE_CMD_TURN          0x13
#define BLE_CMD_MOVE_DIST     0x14
#define BLE_CMD_SET_ARCADE    0x15
#define BLE_CMD_SET_BASE_SPEED 0x16
#define BLE_CMD_LINE_FOLLOW   0x17
#define BLE_CMD_THROTTLE_SET  0x18
#define BLE_CMD_BRAKE         0x19

#define BLE_CMD_PARAM_READ    0x30
#define BLE_CMD_PARAM_WRITE   0x31
#define BLE_CMD_PARAM_SAVE    0x32
#define BLE_CMD_PARAM_LOAD    0x33
#define BLE_CMD_PARAM_LIST    0x34
/* 速度自适应 PID 曲线：App 只在调试静止状态编辑，MCU 本地插值执行。 */
#define BLE_CMD_PID_SCHEDULE_READ   0x35
#define BLE_CMD_PID_SCHEDULE_WRITE  0x36
#define BLE_CMD_PID_SCHEDULE_COMMIT 0x37
#define BLE_CMD_PID_SCHEDULE_AUTOTUNE 0x38
/* 通用底层闭环台架测试：不依赖竞赛状态机。请求 payload: mode:u8, speedNode:u8。 */
#define BLE_CMD_BENCH_START           0x39
#define BLE_CMD_BENCH_ABORT           0x3A
/* 独立循迹入口，payload: speedNode:u8。直线与曲线绝不混用 PID。 */
#define BLE_CMD_BENCH_LINE_STRAIGHT_START 0x3B
#define BLE_CMD_BENCH_LINE_CURVE_START    0x3C

#define BLE_CMD_TEACH_START   0x50
#define BLE_CMD_TEACH_STOP    0x51
#define BLE_CMD_TEACH_PLAY    0x52
#define BLE_CMD_TEACH_PAUSE   0x53
#define BLE_CMD_TEACH_RESUME  0x54
#define BLE_CMD_TEACH_ABORT   0x55
#define BLE_CMD_TEACH_WAYPOINT 0x56
#define BLE_CMD_TEACH_CLEAR   0x57
#define BLE_CMD_TEACH_SAVE    0x58
#define BLE_CMD_TEACH_LOAD    0x59
#define BLE_CMD_TEACH_LIST    0x5A
#define BLE_CMD_TEACH_DELETE  0x5B
#define BLE_CMD_TEACH_RECORD_WP 0x5C

#define BLE_CMD_TELEM_START   0x70
#define BLE_CMD_TELEM_STOP    0x71
#define BLE_CMD_TELEM_FRAME   0x72
#define BLE_CMD_IMU_CAL       0x73
#define BLE_CMD_YAW_ZERO      0x74
#define BLE_CMD_MOTOR_STATUS  0x75
#define BLE_CMD_LINE_STATUS   0x76
#define BLE_CMD_GRAY_CAL      0x77
#define BLE_CMD_PID_ADJUST    0x78
#define BLE_CMD_LINE_START    0x79
#define BLE_CMD_LINE_STOP     0x7A
#define BLE_CMD_SPEED_TUNE    0x7B
/* 台架测试结束结果，payload 见 app_template.c 的 send_result()。 */
#define BLE_CMD_BENCH_RESULT  0x7C
/* H题专用实时循迹调试模式，payload: enable:u8。
 * enable=1：暂停OLED刷新，保持IMU更新，允许App观察单圈运行遥测；
 * enable=0：退出调试策略并恢复正式比赛的I2C分配。 */
#define BLE_CMD_H1_DEBUG_MODE 0x7D
/* H题静止标定采集：请求 [kind,method]；结果由CAL_RESULT异步回传。
 * kind: 1=陀螺仪60秒, 2=白底灰度20次; method: 灰度0=均值, 1=最小值。 */
#define BLE_CMD_CAL_CAPTURE   0x7E
#define BLE_CMD_CAL_RESULT    0x7F

/* 台架结果 reason */
#define BLE_BENCH_REASON_TARGET_REACHED 0x00
#define BLE_BENCH_REASON_LINE_LOST      0x01
#define BLE_BENCH_REASON_TIMEOUT        0x02

/* ---- 指令示教 (0x60~0x6F) ---- */
#define BLE_CMD_INS_CLEAR     0x60
#define BLE_CMD_INS_APPEND    0x61
#define BLE_CMD_INS_INSERT    0x62
#define BLE_CMD_INS_DELETE    0x63
#define BLE_CMD_INS_EXEC      0x64
#define BLE_CMD_INS_STOP      0x65
#define BLE_CMD_INS_PAUSE     0x66
#define BLE_CMD_INS_RESUME    0x67
#define BLE_CMD_INS_STEP      0x68
#define BLE_CMD_INS_GET_PC    0x69
#define BLE_CMD_INS_EVENT     0x6A
#define BLE_CMD_INS_SET_BP    0x6B
#define BLE_CMD_INS_GET_BUF   0x6C

#define BLE_CMD_RAW           0xF0

/* ---- 调试消息 (0x80~0x8F) ---- */
#define BLE_CMD_DEBUG_MSG      0x80
#define BLE_CMD_GET_DEBUG      0x82
#define BLE_CMD_CLEAR_DEBUG    0x83
/* Ball-balance extension:
 * BALL_TELEM is MCU -> App only. BALL_TUNE_SET carries one complete gain pair
 * with an application version and XOR8; it is accepted only in a settled,
 * valid visual-tracking state. */
#define BLE_CMD_BALL_TELEM     0x84
#define BLE_CMD_BALL_TUNE_SET  0x85

/* 错误码 */
#define BLE_ERR_OK            0x00
#define BLE_ERR_UNSUPPORTED   0x01
#define BLE_ERR_BAD_PARAM     0x02
#define BLE_ERR_BUSY          0x03
#define BLE_ERR_FLASH         0x04

#define BLE_SYNC_BYTE         0xAA
#define BLE_MAX_PAYLOAD       250
#define BLE_PKT_TIMEOUT_MS    100

/* 接收到的数据包 */
typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[BLE_MAX_PAYLOAD];
} ble_packet_t;

/* ==================== API ==================== */

void BLE_Protocol_Init(void);

/**
 * 设置时钟源回调 (由 ble.c 或 empty.c 注入)。
 * 用于 RX 超时检测。
 */
void BLE_Protocol_SetClock(uint32_t (*fn)(void));

/**
 * 从 UART ISR 喂入单个字节。
 * 此函数 ISR 安全 (无阻塞、无堆分配)。
 */
void BLE_Protocol_Feed(uint8_t byte);

/**
 * 检查是否有完整包就绪。
 * 主循环调用，10ms 一次。
 * 返回 true 时通过 BLE_Protocol_GetPacket() 取包。
 */
bool BLE_Protocol_Poll(void);

/**
 * 获取已就绪的数据包 (read once)。
 * 必须在 BLE_Protocol_Poll() 返回 true 之后调用。
 */
const ble_packet_t *BLE_Protocol_GetPacket(void);

/**
 * 消费完当前包，释放接收缓冲准备下一包。
 */
void BLE_Protocol_Done(void);

/**
 * 发送数据包。payload 为 NULL 时发空包。
 * 返回 true 表示已排入 MCU→App 非阻塞 TX 队列；false 表示队列满或参数非法。
 */
bool BLE_Protocol_Send(uint8_t cmd, const uint8_t *payload, uint8_t len);

/**
 * 快捷发送 ACK / NACK
 */
static inline void BLE_Protocol_SendAck(uint8_t cmd)
{
    uint8_t echo = cmd;
    BLE_Protocol_Send(BLE_CMD_ACK, &echo, 1);
}

static inline void BLE_Protocol_SendNack(uint8_t cmd, uint8_t err)
{
    uint8_t buf[2] = {cmd, err};
    BLE_Protocol_Send(BLE_CMD_NACK, buf, 2);
}

#endif /* __BLE_PROTOCOL_H */
