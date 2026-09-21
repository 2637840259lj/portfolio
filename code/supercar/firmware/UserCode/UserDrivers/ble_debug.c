/**
 * @file    ble_debug.c
 * @brief   调试消息推送 — 实现
 */

#include "ble_debug.h"
#include "ble_protocol.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define DBG_BUF_SIZE  512
#define DBG_MSG_MAX   120   /* 单条消息最大字节 (含头) */

static char   dbg_ring[DBG_BUF_SIZE];
static uint16_t dbg_head;  /* 写入位置 */
static uint16_t dbg_tail;  /* 发送位置 (App 未取走的起始) */
static uint16_t dbg_count; /* 待发送消息数 */

void BLE_DebugInit(void)
{
    memset(dbg_ring, 0, DBG_BUF_SIZE);
    dbg_head  = 0;
    dbg_tail  = 0;
    dbg_count = 0;
}

void BLE_DebugPush(uint8_t level, const char *text)
{
    uint8_t  tlen = (uint8_t)strlen(text);
    uint8_t  total = 2 + tlen; /* level + count + text */
    uint16_t i;

    if (dbg_count >= 20) {
        /* 缓冲区满, 丢弃最旧的一条 */
        /* 跳过旧消息: 读其长度, 前进 tail */
        uint16_t pos = dbg_tail;
        uint8_t  old_len;
        for (i = 0; i < 1; i++) { /* 只丢一条 */
            if (dbg_count == 0) break;
            old_len = (uint8_t)dbg_ring[(pos + 1) % DBG_BUF_SIZE];
            pos = (pos + 2 + old_len) % DBG_BUF_SIZE;
            dbg_count--;
        }
        dbg_tail = pos;
    }

    /* 写入: level */
    dbg_ring[dbg_head] = level;
    dbg_head = (dbg_head + 1) % DBG_BUF_SIZE;

    /* 写入: count (text length) */
    dbg_ring[dbg_head] = tlen + 1;
    dbg_head = (dbg_head + 1) % DBG_BUF_SIZE;

    /* 写入: text */
    for (i = 0; i <= tlen && i < (DBG_MSG_MAX - 2); i++) {
        dbg_ring[dbg_head] = text[i];
        dbg_head = (dbg_head + 1) % DBG_BUF_SIZE;
    }

    dbg_count++;
}

void BLE_DebugPrintf(uint8_t level, const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    BLE_DebugPush(level, buf);
}

void BLE_DebugFlush(void)
{
    /* 发送所有未取走的消息 */
    while (dbg_count > 0) {
        uint8_t level = (uint8_t)dbg_ring[dbg_tail];
        uint8_t count = (uint8_t)dbg_ring[(dbg_tail + 1) % DBG_BUF_SIZE];
        uint8_t payload[DBG_MSG_MAX];
        uint16_t i;
        payload[0] = level;
        payload[1] = count;
        for (i = 0; i < count && i < (DBG_MSG_MAX - 2); i++) {
            payload[2 + i] = dbg_ring[(dbg_tail + 2 + i) % DBG_BUF_SIZE];
        }
        BLE_Protocol_Send(BLE_CMD_DEBUG_MSG, payload, 2 + count);

        dbg_tail = (dbg_tail + 2 + count) % DBG_BUF_SIZE;
        dbg_count--;
    }
}

void BLE_DebugClear(void)
{
    dbg_head  = 0;
    dbg_tail  = 0;
    dbg_count = 0;
    memset(dbg_ring, 0, DBG_BUF_SIZE);
}

/* 主循环调用: 不做任何事, 消息由 App 主动拉取 */
void BLE_DebugPoll(void)
{
    /* debug messages are pulled by App via GET_DEBUG */
}
