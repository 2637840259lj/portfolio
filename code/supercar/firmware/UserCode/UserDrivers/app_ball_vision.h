/**
 * @file app_ball_vision.h
 * @brief K230 球位置通信模块 (UART2, PB15/PB16, 115200 8N1)
 *
 * K230 通过串口发送球在摆杆上的位置坐标。兼容两种帧格式:
 *   旧格式: $BALL,x,y\r\n
 *   完整格式: $BALL,valid,x,y,vx,vy,confidence,seq\r\n
 * x/y 是像素或 mm（由 K230 端定义）；vx/vy 为像素/秒；valid=0 表示本帧失检。
 * MCU 侧 UART2 RX 中断接收、行缓冲解析，提供最新视觉快照给后续平衡模块。
 */
#ifndef APP_BALL_VISION_H
#define APP_BALL_VISION_H

#include <stdint.h>
#include <stdbool.h>

/* 球位置数据 */
typedef struct {
    int16_t x;          /* 球 X 坐标 (像素或 mm, 由 K230 定义) */
    int16_t y;          /* 球 Y 坐标 */
    int16_t vx;         /* X 速度；旧格式帧固定为 0 */
    int16_t vy;         /* Y 速度；旧格式帧固定为 0 */
    uint8_t confidence; /* 0~100；旧格式帧固定为 100 */
    uint32_t seq;       /* K230 帧序号；旧格式帧使用本地计数 */
    uint32_t timestamp; /* 接收时刻 (ms, 来自 sys_time) */
    bool     valid;     /* 当前帧是否为有效球位置 */
    uint32_t frame_cnt; /* 累计完成解析的帧数 */
} BallPosition_t;

/**
 * @brief 初始化球位置通信模块
 *   在 UART2 (UART_K230) 硬件初始化之后调用.
 */
void BallVision_Init(void);

/**
 * @brief UART2 RX 中断中调用 — 逐字节喂入解析器
 *   在 UART2_IRQHandler 里调用.
 */
void BallVision_OnRxByte(uint8_t b);

/**
 * @brief 获取最新球位置 (线程安全, 简单关中断读)
 */
BallPosition_t BallVision_GetPosition(void);

/**
 * @brief 球位置数据是否有效 (收到过至少一帧)
 */
bool BallVision_IsActive(void);

/**
 * @brief 距上次收到数据的毫秒数 (用于超时检测)
 */
uint32_t BallVision_TimeSinceLastFrame(void);

/**
 * @brief UART2 出现接收错误时由中断服务程序调用，丢弃当前半帧。
 */
void BallVision_OnRxError(void);

/**
 * @brief 获取 UART2 累计接收错误次数（overrun/parity/framing 等）。
 */
uint32_t BallVision_GetRxErrorCount(void);

#endif /* APP_BALL_VISION_H */
