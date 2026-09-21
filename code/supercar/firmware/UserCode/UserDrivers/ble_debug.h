#ifndef __BLE_DEBUG_H
#define __BLE_DEBUG_H

/**
 * @file    ble_debug.h
 * @brief   调试消息推送 — printf 同步写入 BLE
 *
 * 用法: 在 empty.c 的 DEBUG_PRINTF 宏中替换 printf,
 *       或在关键位置调用 BLE_DebugPrintf()。
 */

#include <stdint.h>

/* 调试级别 */
#define DBG_INFO  0
#define DBG_WARN  1
#define DBG_ERR   2
#define DBG_DEBUG 3

void BLE_DebugInit(void);

/**
 * 推送一条调试消息到 BLE。
 * 格式: BLE_CMD_DEBUG_MSG [level:u8] [count:u8] [text...]
 * count = strlen(text) + 1 (含 '\0')
 */
void BLE_DebugPush(uint8_t level, const char *text);

/**
 * printf 风格, 自动格式化到栈缓冲然后推送
 */
void BLE_DebugPrintf(uint8_t level, const char *fmt, ...);

/**
 * 主循环调用: 检查调试缓冲区是否有待发送数据
 */
void BLE_DebugPoll(void);

/**
 * App 请求拉取: 发送缓冲区内全部已存消息
 */
void BLE_DebugFlush(void);

void BLE_DebugClear(void);

#endif /* __BLE_DEBUG_H */
