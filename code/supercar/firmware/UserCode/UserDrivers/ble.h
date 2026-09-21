#ifndef __BLE_H
#define __BLE_H

#include <stdint.h>
#include <stdbool.h>
#include "ti_msp_dl_config.h"

/* CH9141 BLE 蓝牙透传模块驱动
 * 硬件: 沁恒 CH9141, UART_BLE@115200
 * 协议: ASCII (App→MCU) + 二进制 (MCU→App 应答) */#include "ti_msp_dl_config.h"

// RX 环形缓冲区
#define BLE_RX_RING_BUF_SIZE 256
extern uint8_t ble_rx_ring_buf[BLE_RX_RING_BUF_SIZE];
extern volatile uint16_t ble_rx_head;
extern volatile uint16_t ble_rx_tail;
extern volatile uint16_t ble_rx_count;

void BLE_RxBufferInit(void);
void BLE_RxBufferEnqueue(uint8_t data);
uint8_t BLE_RxBufferDequeue(void);
uint16_t BLE_RxBufferCount(void);


void BLE_Init(void);
// void BLE_Poll(void); // RX 改为中断，不再需要轮询
void BLE_RxHandler(uint8_t byte); // 从 ring buffer 取数据处理
void BLE_SendByte(uint8_t data);
void BLE_SendString(const char *str);

/* MCU→App 非阻塞回传队列：调用方仅入队，UART3 TX FIFO 中断负责逐步发送。 */
#define BLE_TX_PRIO_SAFETY  0U  /* ACK/NACK、急停/刹车结果 */
#define BLE_TX_PRIO_CONTROL 1U  /* 校准/任务结果、普通状态 */
#define BLE_TX_PRIO_TELEM   2U  /* 遥测与调试，可在满载时丢弃 */
#define BLE_TX_FRAME_MAX    254U /* 0xAA + LEN + CMD + 250B payload + XOR */
void     BLE_TxInit(void);
bool     BLE_TxEnqueue(const uint8_t *data, uint16_t len, uint8_t priority);
void     BLE_TxIrqHandler(void);
uint16_t BLE_TxPendingBytes(void);
uint32_t BLE_TxDroppedFrames(void);
/* 停止遥测时丢弃尚未发送的低优先级遥测帧，保留 ACK/状态帧。 */
void     BLE_TxDropTelemetry(void);
/* 独占任务启动前丢弃未发送的非 ACK/NACK 帧，让最终确认包不被旧 PING/状态帧堵塞。 */
void     BLE_TxDropNonSafety(void);

/* ASCII 协议 */
void        BLE_ParseAscii(const char *line);
int         BLE_AsciiReady(void);
void        BLE_AsciiClear(void);
const char* BLE_GetCmd(void);

/* L/R 差速直接存储（App 算好左右轮速度，MCU 直接执行） */
uint32_t    BLE_GetLastCmdMs(void);
int         BLE_GetLeft(void);
int         BLE_GetRight(void);
int         BLE_GetDriveActive(void);
void        BLE_ResetDrive(void);

/* S0 安全锁：收到安全命令的解析瞬间关闭实时 L/R，避免等待主循环消费队列时旧目标继续执行。 */
void        BLE_EnterSafetyLock(void);
void        BLE_ClearSafetyLock(void);
uint8_t     BLE_IsSafetyLocked(void);

/* AT 模式 */
void BLE_EnterATMode(void);
void BLE_ExitATMode(void);

#endif
