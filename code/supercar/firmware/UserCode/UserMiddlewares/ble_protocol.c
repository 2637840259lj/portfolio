/**
 * @file    ble_protocol.c
 * @brief   蓝牙二进制协议编解码实现
 */

#include "ble_protocol.h"
#include "ble.h"
#include <string.h>
#include "ti_msp_dl_config.h"
#include <stdio.h>
#include <ti/driverlib/driverlib.h>

/* ==================== 接收状态机 ==================== */
typedef enum {
    RX_SYNC = 0,
    RX_LEN,
    RX_CMD,
    RX_DATA,
    RX_XOR
} rx_state_t;

static rx_state_t    rx_state;
static uint8_t       rx_len;
static uint8_t       rx_cmd;
static uint8_t       rx_idx;
static uint8_t       rx_xor;
static uint32_t      rx_last_ms;

static ble_packet_t  rx_pkt;
/* 当前正在组装的帧必须与已就绪帧分离，否则突发第二帧会覆盖第一帧，
 * 造成 ACK/状态/遥测解析错位，表现为页面数据归零。 */
static uint8_t  rx_data[BLE_MAX_PAYLOAD];
static volatile bool rx_ready;
static bool     rx_pending;
static uint8_t  pending_cmd;
static uint8_t  pending_len;
static uint8_t  pending_data[BLE_MAX_PAYLOAD];

/* ==================== 发送缓冲 ==================== */
static uint8_t  tx_buf[4 + BLE_MAX_PAYLOAD]; /* SYNC + LEN + CMD + max data + XOR */

/* ==================== 获取时钟 (ms) ==================== */
/* 依赖 sys_time.h 的 mspm0_get_clock_ms(), 但协议层不直接依赖 BSP。
   clock_ms_fn 由初始化时注入, 也可在 ble.c 层桥接 */
static uint32_t (*clock_ms)(void) = NULL;

void BLE_Protocol_Init(void)
{
    rx_state  = RX_SYNC;
    rx_len    = 0;
    rx_cmd    = 0;
    rx_idx    = 0;
    rx_xor    = 0;
    rx_last_ms = 0;
    rx_ready  = false;
    rx_pending = false;
    memset(&rx_pkt, 0, sizeof(rx_pkt));
}

/**
 * 设置时钟源回调 (由 ble.c 或 empty.c 注入)。
 * 用于 RX 超时检测。
 */
void BLE_Protocol_SetClock(uint32_t (*fn)(void))
{
    clock_ms = fn;
}

/* ==================== 接收: 字节喂入 ==================== */
void BLE_Protocol_Feed(uint8_t byte)
{
    uint32_t now = clock_ms ? clock_ms() : 0;

    /* 超时检测: 非 SYNC 状态下超过 100ms 无新字节则复位 */
    if (rx_state != RX_SYNC && clock_ms) {
        if (now - rx_last_ms > BLE_PKT_TIMEOUT_MS) {
            rx_state = RX_SYNC;
        }
    }
    rx_last_ms = now;

    switch (rx_state) {

    case RX_SYNC:
        if (byte == BLE_SYNC_BYTE) {
            rx_state = RX_LEN;
            rx_xor   = 0;
        }
        break;

    case RX_LEN:
        if (byte > BLE_MAX_PAYLOAD) {
            /* 长度非法, 丢弃并重新搜索帧头 */
            rx_state = RX_SYNC;
            return;
        }
        rx_len   = byte;
        rx_xor  ^= byte;
        rx_state = RX_CMD;
        break;

    case RX_CMD:
        rx_cmd   = byte;
        rx_xor  ^= byte;
        if (rx_len == 0) {
            /* 无 payload, 直接跳到校验 */
            rx_state = RX_XOR;
        } else {
            rx_idx   = 0;
            rx_state = RX_DATA;
        }
        break;

    case RX_DATA:
        rx_data[rx_idx++] = byte;
        rx_xor ^= byte;
        if (rx_idx >= rx_len) {
            rx_state = RX_XOR;
        }
        break;

    case RX_XOR:
        if (byte == rx_xor) {
            if (!rx_ready) {
                rx_pkt.cmd = rx_cmd;
                rx_pkt.len = rx_len;
                memcpy(rx_pkt.data, rx_data, rx_len);
                rx_ready   = true;
            } else if (!rx_pending) {
                /* 第二包暂存, 等第一包消费后自动提升 */
                pending_cmd = rx_cmd;
                pending_len = rx_len;
                memcpy(pending_data, rx_data, rx_len);
                rx_pending  = true;
            }
        }
        /* 无论成败, 回到 SYNC */
        rx_state = RX_SYNC;
        break;

    default:
        rx_state = RX_SYNC;
        break;
    }
}

/* ==================== 主循环轮询 ==================== */
bool BLE_Protocol_Poll(void)
{
    return rx_ready;
}

const ble_packet_t *BLE_Protocol_GetPacket(void)
{
    return &rx_pkt;
}

void BLE_Protocol_Done(void)
{
    if (rx_pending) {
        /* 有暂存包, 提升为当前包 (避免下次 Poll 前被新数据覆盖) */
        rx_pkt.cmd = pending_cmd;
        rx_pkt.len = pending_len;
        memcpy(rx_pkt.data, pending_data, pending_len);
        rx_pending = false;
        /* rx_ready 保持 true */
    } else {
        rx_ready = false;
        memset(&rx_pkt, 0, sizeof(rx_pkt));
    }


}

/* ==================== 发送 ==================== */
bool BLE_Protocol_Send(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint8_t xor8 = len ^ cmd;
    uint8_t priority = BLE_TX_PRIO_CONTROL;
    uint16_t i;

    if (len > BLE_MAX_PAYLOAD || (len > 0U && payload == NULL)) return false;
    if (cmd == BLE_CMD_ACK || cmd == BLE_CMD_NACK) {
        priority = BLE_TX_PRIO_SAFETY;
    } else if (cmd == BLE_CMD_TELEM_FRAME || cmd == BLE_CMD_DEBUG_MSG ||
               cmd == BLE_CMD_BALL_TELEM) {
        priority = BLE_TX_PRIO_TELEM;
    }

    tx_buf[0] = BLE_SYNC_BYTE;
    tx_buf[1] = len;
    tx_buf[2] = cmd;
    for (i = 0; i < len; i++) {
        tx_buf[3 + i] = payload[i];
        xor8 ^= payload[i];
    }
    tx_buf[3 + len] = xor8;

    /* CH9141 串口透传：协议帧直接进入 UART3 发送队列。 */
    return BLE_TxEnqueue(tx_buf, (uint16_t)(4U + len), priority);
}
