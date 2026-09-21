/**
 * @file app_ball_vision.c
 * @brief K230 球位置通信模块 — UART2 RX 中断接收 + 行解析
 *
 * 协议兼容:
 *   旧格式:   $BALL,x,y\r\n
 *   完整格式: $BALL,valid,x,y,vx,vy,confidence,seq\r\n
 * RX 中断只完成逐字节行缓冲和受限 sscanf 解析；不做 printf、控制计算或阻塞操作。
 */
#include "app_ball_vision.h"
#include "sys_time.h"
#include <stdio.h>

/* 单行最长 79 字节；完整帧实际小于 64 字节，超长直接丢弃。 */
#define BALL_LINE_MAX_LEN 80U

static volatile BallPosition_t s_ball_pos;
static volatile uint32_t s_rx_error_count;
static char s_line[BALL_LINE_MAX_LEN];
static uint8_t s_line_len;
static bool s_collecting;

static void parse_reset(void)
{
    s_line_len = 0U;
    s_collecting = false;
    s_line[0] = '\0';
}

/* 仅接受已限定字段数和范围的行，避免异常串口内容进入后续控制。 */
static void parse_commit(void)
{
    unsigned int valid = 1U;
    unsigned int confidence = 100U;
    unsigned long seq = 0UL;
    int x = 0, y = 0, vx = 0, vy = 0;
    int fields;
    BallPosition_t pos;

    fields = sscanf(s_line, "$BALL,%u,%d,%d,%d,%d,%u,%lu",
                    &valid, &x, &y, &vx, &vy, &confidence, &seq);
    if (fields == 2) {
        /* 旧格式 $BALL,x,y：第一字段刚被解析为 valid，需重新读取。 */
        fields = sscanf(s_line, "$BALL,%d,%d", &x, &y);
        valid = 1U;
        confidence = 100U;
        vx = 0;
        vy = 0;
        seq = s_ball_pos.frame_cnt + 1U;
    }

    if ((fields != 2 && fields != 7) || valid > 1U ||
        x < -4096 || x > 4095 || y < -4096 || y > 4095 ||
        vx < -9999 || vx > 9999 || vy < -9999 || vy > 9999 ||
        confidence > 100U) {
        s_rx_error_count++;
        return;
    }

    /* A detector miss carries no usable position. Keep the last valid snapshot
     * until its normal freshness timeout expires: one invalid $BALL frame must
     * not replace a real x/vx with zero or interrupt motor tracking, while a
     * sustained loss still reaches the existing 120 ms safety timeout. */
    if (valid == 0U) {
        return;
    }

    pos.x = (int16_t)x;
    pos.y = (int16_t)y;
    pos.vx = (int16_t)vx;
    pos.vy = (int16_t)vy;
    pos.confidence = (uint8_t)confidence;
    pos.seq = (uint32_t)seq;
    pos.timestamp = g_system_ticks_ms;
    pos.valid = true;
    pos.frame_cnt = s_ball_pos.frame_cnt + 1U;
    s_ball_pos = pos;
}

/* ── 公开接口 ── */

void BallVision_Init(void)
{
    parse_reset();
    s_ball_pos.x = 0;
    s_ball_pos.y = 0;
    s_ball_pos.vx = 0;
    s_ball_pos.vy = 0;
    s_ball_pos.confidence = 0U;
    s_ball_pos.seq = 0U;
    s_ball_pos.timestamp = 0U;
    s_ball_pos.valid = false;
    s_ball_pos.frame_cnt = 0U;
    s_rx_error_count = 0U;
}

void BallVision_OnRxByte(uint8_t b)
{
    if (b == '$') {
        s_line_len = 0U;
        s_collecting = true;
        s_line[s_line_len++] = (char)b;
        return;
    }

    if (!s_collecting) return;

    if (b == '\n') {
        s_line[s_line_len] = '\0';
        parse_commit();
        parse_reset();
        return;
    }

    if (b == '\r') return;

    if (s_line_len < BALL_LINE_MAX_LEN - 1U) {
        s_line[s_line_len++] = (char)b;
    } else {
        s_rx_error_count++;
        parse_reset();
    }
}

BallPosition_t BallVision_GetPosition(void)
{
    /* ISR 逐字段发布，主循环必须在同一临界区复制完整快照。
       保存 PRIMASK，避免从其它临界区调用时错误重新打开中断。 */
    BallPosition_t pos;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    pos = s_ball_pos;
    if (primask == 0U) {
        __enable_irq();
    }
    return pos;
}

bool BallVision_IsActive(void)
{
    return BallVision_GetPosition().valid;
}

uint32_t BallVision_TimeSinceLastFrame(void)
{
    BallPosition_t pos = BallVision_GetPosition();
    if (!pos.valid) return 0xFFFFFFFFU;
    return g_system_ticks_ms - pos.timestamp;
}

void BallVision_OnRxError(void)
{
    s_rx_error_count++;
    parse_reset();
}

uint32_t BallVision_GetRxErrorCount(void)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t count;
    __disable_irq();
    count = s_rx_error_count;
    if (primask == 0U) {
        __enable_irq();
    }
    return count;
}
