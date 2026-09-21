/**
 * @file    ble.c
 * @brief   CH9141 蓝牙串口透传模块驱动
 *
 * 基于 MSPM0G3507 DriverLib, UART2 中断接收。
 * 蓝牙模块出厂默认: 从机模式, 115200/8N1, 设备名 "CH9141BLE2U"。
 *
 * 通信约定:
 *   1. ASCII 固定 4 字节命令：App→MCU 控制与查询
 *      Lxx\n / Rxx\n、PNG\n、STP\n、EMG\n、BRK\n、HW?\n、TM+\n、TM-\n、CAL\n、YZR\n
 *   2. 二进制协议 (ble_protocol)：MCU→App 应答、状态与遥测回传。
 */

#include "ble.h"
#include "ble_protocol.h"
#include "sys_time.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef UART_BLE_INST
#error "UART_BLE 实例未在 SysConfig 中生成!"
#endif

/* ── RX 环形缓冲区 ── */
uint8_t ble_rx_ring_buf[BLE_RX_RING_BUF_SIZE];
volatile uint16_t ble_rx_head = 0;
volatile uint16_t ble_rx_tail = 0;
volatile uint16_t ble_rx_count = 0;

void BLE_RxBufferInit(void) {
    ble_rx_head = 0;
    ble_rx_tail = 0;
    ble_rx_count = 0;
}

void BLE_RxBufferEnqueue(uint8_t data) {
    if (ble_rx_count < BLE_RX_RING_BUF_SIZE) {
        ble_rx_ring_buf[ble_rx_tail] = data;
        ble_rx_tail = (ble_rx_tail + 1) % BLE_RX_RING_BUF_SIZE;
        ble_rx_count++;
    }
}

uint8_t BLE_RxBufferDequeue(void) {
    uint8_t data = 0;
    if (ble_rx_count > 0) {
        data = ble_rx_ring_buf[ble_rx_head];
        ble_rx_head = (ble_rx_head + 1) % BLE_RX_RING_BUF_SIZE;
        ble_rx_count--;
    }
    return data;
}

uint16_t BLE_RxBufferCount(void) {
    return ble_rx_count;
}

/* ── ASCII 行解析器 ── */
static char   ascii_buf[64];
static int    ascii_idx = 0;

/* 非实时命令 FIFO：主循环每 10ms 只消费一条命令，不能让后来的 PING/MST 覆盖
 * CAL、自动整定等先到的重要控制命令。L/R 仍走独立实时通道。 */
#define ASCII_CMD_QUEUE_SIZE 8
#define ASCII_PRIO_SAFETY    0U  /* EMG/BRK/STP/AT-/LF-：抢占并取消当前任务 */
#define ASCII_PRIO_TASK      1U  /* CAL/GCL/AT+/LF+：获得独占控制权 */
#define ASCII_PRIO_CONTROL   2U  /* 遥测开关、归零、参数调整 */
#define ASCII_PRIO_POLL      3U  /* PNG/MST/LST/HW?：可合并、可丢弃 */
static char   ascii_cmd_queue[ASCII_CMD_QUEUE_SIZE][64];
static uint8_t ascii_cmd_priority[ASCII_CMD_QUEUE_SIZE];
static uint8_t ascii_cmd_head = 0;
static uint8_t ascii_cmd_tail = 0;
static uint8_t ascii_cmd_count = 0;
static uint8_t ascii_cmd_selected = ASCII_CMD_QUEUE_SIZE;

/* L/R 差速直接存储（App 算差速，MCU 直接执行） */
/* 注意：LST 是循迹状态查询，不能把任何 L 开头的 3 字符串误判为 Lxx 电机指令。 */
static int ascii_is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
}

static int    ble_left_val  = 0;   /* -100~100 */
static int    ble_right_val = 0;   /* -100~100 */
static int    ble_drive_active = 0;/* 收到 L/R 后置1, STP/BRK/EMG 清0 */
/* S0/S1 后短暂屏蔽实时 L/R，吸收 CH9141 中可能残留的旧4字节包；不是永久锁车。 */
#define BLE_SAFETY_GUARD_MS 120U
static uint32_t ble_safety_until_ms = 0;
static uint32_t ble_last_cmd_ms = 0;

/* ==================== MCU→App UART3 非阻塞 TX 队列 ====================
 * 以帧为单位排队，保留二进制协议边界。主循环仅入队，UART TX FIFO 阈值中断逐步搬运。
 * 每帧最大 254B（同步/长度/命令/250B负载/XOR），队列上限8帧；满载优先丢遥测。 */
#define BLE_TX_QUEUE_SIZE  8U

typedef struct {
    uint8_t data[BLE_TX_FRAME_MAX];
    uint16_t len;
    uint8_t priority;
} ble_tx_frame_t;

static ble_tx_frame_t tx_queue[BLE_TX_QUEUE_SIZE];
static volatile uint8_t tx_count = 0;
static volatile uint8_t tx_active_valid = 0;
static volatile uint16_t tx_active_pos = 0;
static uint16_t tx_active_len = 0;
static uint8_t tx_active_data[BLE_TX_FRAME_MAX];
static volatile uint32_t tx_dropped_frames = 0;

static uint32_t BLE_TxLock(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}
static void BLE_TxUnlock(uint32_t primask)
{
    if (primask == 0U) __enable_irq();
}

static void BLE_TxStartNextLocked(void)
{
    uint8_t i, best = 0, best_prio = 0xFF;
    if (tx_active_valid || tx_count == 0U) return;
    for (i = 0; i < tx_count; i++) {
        if (tx_queue[i].priority < best_prio) {
            best_prio = tx_queue[i].priority;
            best = i;
        }
    }
    tx_active_len = tx_queue[best].len;
    memcpy(tx_active_data, tx_queue[best].data, tx_active_len);
    for (i = best; (uint8_t)(i + 1U) < tx_count; i++) tx_queue[i] = tx_queue[i + 1U];
    tx_count--;
    tx_active_pos = 0;
    tx_active_valid = 1;
}

static void BLE_TxFillFifoLocked(void)
{
    while (!DL_UART_isTXFIFOFull(UART_BLE_INST)) {
        BLE_TxStartNextLocked();
        if (!tx_active_valid) break;
        DL_UART_Main_transmitData(UART_BLE_INST, tx_active_data[tx_active_pos++]);
        if (tx_active_pos >= tx_active_len) {
            tx_active_valid = 0;
            tx_active_pos = 0;
        }
    }
    if (tx_active_valid || tx_count > 0U) {
        DL_UART_enableInterrupt(UART_BLE_INST, DL_UART_INTERRUPT_TX);
    } else {
        DL_UART_disableInterrupt(UART_BLE_INST, DL_UART_INTERRUPT_TX);
    }
}

void BLE_TxInit(void)
{
    tx_count = 0;
    tx_active_valid = 0;
    tx_active_pos = 0;
    tx_active_len = 0;
    tx_dropped_frames = 0;
    DL_UART_disableInterrupt(UART_BLE_INST, DL_UART_INTERRUPT_TX);
    DL_UART_clearInterruptStatus(UART_BLE_INST, DL_UART_INTERRUPT_TX);
    NVIC_ClearPendingIRQ(UART_BLE_INST_INT_IRQN);
    NVIC_EnableIRQ(UART_BLE_INST_INT_IRQN);
}

bool BLE_TxEnqueue(const uint8_t *data, uint16_t len, uint8_t priority)
{
    uint8_t i, worst = 0, worst_prio = 0;
    uint32_t primask;
    if (data == NULL || len == 0U || len > BLE_TX_FRAME_MAX) return false;
    primask = BLE_TxLock();
    if (tx_count >= BLE_TX_QUEUE_SIZE) {
        for (i = 0; i < tx_count; i++) {
            if (tx_queue[i].priority >= worst_prio) {
                worst_prio = tx_queue[i].priority;
                worst = i;
            }
        }
        /* 只允许更高优先级帧替换低优先级帧；遥测/调试在拥塞时主动丢弃。 */
        if (worst_prio <= priority) {
            tx_dropped_frames++;
            BLE_TxUnlock(primask);
            return false;
        }
        for (i = worst; (uint8_t)(i + 1U) < tx_count; i++) tx_queue[i] = tx_queue[i + 1U];
        tx_count--;
        tx_dropped_frames++;
    }
    memcpy(tx_queue[tx_count].data, data, len);
    tx_queue[tx_count].len = len;
    tx_queue[tx_count].priority = priority;
    tx_count++;
    BLE_TxFillFifoLocked();
    BLE_TxUnlock(primask);
    return true;
}

void BLE_TxIrqHandler(void)
{
    /* 调用方 UART3_IRQHandler 已读取 IIDX 并确认这是 TX 中断。
     * 不要在这里再次读取 IIDX：该读取会清除中断来源，第二次常得到
     * NO_INTERRUPT，造成超过 UART FIFO 容量的帧只发送前半段，后续 ACK
     * 也会被这条半帧堵住。 */
    BLE_TxFillFifoLocked();
}

uint16_t BLE_TxPendingBytes(void)
{
    uint8_t i;
    uint32_t primask = BLE_TxLock();
    uint16_t total = tx_active_valid ? (uint16_t)(tx_active_len - tx_active_pos) : 0U;
    for (i = 0; i < tx_count; i++) total = (uint16_t)(total + tx_queue[i].len);
    BLE_TxUnlock(primask);
    return total;
}
uint32_t BLE_TxDroppedFrames(void) { return tx_dropped_frames; }

void BLE_TxDropTelemetry(void)
{
    uint8_t i = 0;
    uint32_t primask = BLE_TxLock();
    while (i < tx_count) {
        if (tx_queue[i].priority >= BLE_TX_PRIO_TELEM) {
            uint8_t j;
            for (j = i; (uint8_t)(j + 1U) < tx_count; j++) tx_queue[j] = tx_queue[j + 1U];
            tx_count--;
        } else {
            i++;
        }
    }
    /* 正在发送的整帧不强行截断，避免向 App 发送半帧；它完成后不再有旧遥测排队。 */
    BLE_TxFillFifoLocked();
    BLE_TxUnlock(primask);
}

void BLE_TxDropNonSafety(void)
{
    uint8_t i = 0;
    uint32_t primask = BLE_TxLock();
    while (i < tx_count) {
        /* ACK/NACK 已是最高优先级，独占任务开始时仅保留它们；
         * 丢弃旧 PING、状态与遥测，避免校准完成 ACK 被历史回包拖延。 */
        if (tx_queue[i].priority != BLE_TX_PRIO_SAFETY) {
            uint8_t j;
            for (j = i; (uint8_t)(j + 1U) < tx_count; j++) tx_queue[j] = tx_queue[j + 1U];
            tx_count--;
            tx_dropped_frames++;
        } else {
            i++;
        }
    }
    BLE_TxFillFifoLocked();
    BLE_TxUnlock(primask);
}

static uint8_t BLE_CommandPriority(const char *cmd)
{
    if (strcmp(cmd, "EMG") == 0 || strcmp(cmd, "BRK") == 0 || strcmp(cmd, "STOP") == 0 ||
        strcmp(cmd, "TELEM_OFF") == 0 || strcmp(cmd, "SPEED_TUNE_STOP") == 0 ||
        strcmp(cmd, "LINE_STOP") == 0) return ASCII_PRIO_SAFETY;
    if (strcmp(cmd, "IMU_CAL") == 0 || strcmp(cmd, "GRAY_CAL") == 0 ||
        strcmp(cmd, "TELEM_ON") == 0 || strcmp(cmd, "SPEED_TUNE_START") == 0 ||
        strcmp(cmd, "LINE_START") == 0) return ASCII_PRIO_TASK;
    if (strcmp(cmd, "PING") == 0 || strcmp(cmd, "MOTOR_STATUS") == 0 ||
        strcmp(cmd, "LINE_STATUS") == 0 || strcmp(cmd, "GET_HW") == 0) return ASCII_PRIO_POLL;
    return ASCII_PRIO_CONTROL;
}

static void BLE_ClearAsciiQueue(void)
{
    ascii_cmd_head = ascii_cmd_tail = ascii_cmd_count = 0;
    ascii_cmd_selected = ASCII_CMD_QUEUE_SIZE;
}

static void BLE_QueueAsciiCmd(const char *cmd)
{
    uint8_t prio = BLE_CommandPriority(cmd);
    uint8_t i, index;
    /* 低优先级轮询命令合并，不能挤占控制/校准通道。 */
    if (prio == ASCII_PRIO_POLL) {
        for (i = 0; i < ascii_cmd_count; i++) {
            index = (uint8_t)((ascii_cmd_head + i) % ASCII_CMD_QUEUE_SIZE);
            if (strcmp(ascii_cmd_queue[index], cmd) == 0) return;
        }
    }
    /* S0 抢占：直接废弃一切尚未执行的普通工作。 */
    if (prio == ASCII_PRIO_SAFETY) BLE_ClearAsciiQueue();
    /* S1 独占任务：清空旧查询/控制，避免校准排在无意义状态轮询之后。 */
    else if (prio == ASCII_PRIO_TASK) BLE_ClearAsciiQueue();

    /* 队列满时，优先替换最低优先级项；没有可替换项则丢弃新低优先命令。 */
    if (ascii_cmd_count >= ASCII_CMD_QUEUE_SIZE) {
        uint8_t worst = ascii_cmd_head, worst_prio = 0;
        for (i = 0; i < ascii_cmd_count; i++) {
            index = (uint8_t)((ascii_cmd_head + i) % ASCII_CMD_QUEUE_SIZE);
            if (ascii_cmd_priority[index] >= worst_prio) {
                worst_prio = ascii_cmd_priority[index]; worst = index;
            }
        }
        if (worst_prio <= prio) return;
        /* 删除中间元素，保持 FIFO 相对顺序；队列满时 tail==head，不能用 tail 作终止条件。 */
        {
            uint8_t offset = (uint8_t)((worst + ASCII_CMD_QUEUE_SIZE - ascii_cmd_head) % ASCII_CMD_QUEUE_SIZE);
            for (i = offset; (uint8_t)(i + 1U) < ascii_cmd_count; i++) {
                uint8_t dst = (uint8_t)((ascii_cmd_head + i) % ASCII_CMD_QUEUE_SIZE);
                uint8_t src = (uint8_t)((ascii_cmd_head + i + 1U) % ASCII_CMD_QUEUE_SIZE);
                strcpy(ascii_cmd_queue[dst], ascii_cmd_queue[src]);
                ascii_cmd_priority[dst] = ascii_cmd_priority[src];
            }
        }
        ascii_cmd_tail = (uint8_t)((ascii_cmd_tail + ASCII_CMD_QUEUE_SIZE - 1U) % ASCII_CMD_QUEUE_SIZE);
        ascii_cmd_count--;
    }
    strncpy(ascii_cmd_queue[ascii_cmd_tail], cmd, 63);
    ascii_cmd_queue[ascii_cmd_tail][63] = '\0';
    ascii_cmd_priority[ascii_cmd_tail] = prio;
    ascii_cmd_tail = (uint8_t)((ascii_cmd_tail + 1U) % ASCII_CMD_QUEUE_SIZE);
    ascii_cmd_count++;
}

void BLE_Init(void)
{
    ascii_idx = 0;
    BLE_ClearAsciiQueue();
    ble_left_val = 0;
    ble_right_val = 0;
    ble_drive_active = 0;
    ble_safety_until_ms = 0;
    DL_UART_enableFIFOs(UART_BLE_INST);
    /* CH9141 上行多为 4 字节 ASCII 短命令。若保持 SysConfig 生成的
     * 1/2 FIFO 门限，短包可能一直滞留在 UART FIFO，直到后续数据到来才被消费。
     * 设为单字节门限后，首个短命令即可及时触发 RX 中断。 */
    DL_UART_Main_setRXFIFOThreshold(UART_BLE_INST, DL_UART_RX_FIFO_LEVEL_ONE_ENTRY);
    BLE_TxInit();
    BLE_RxBufferInit();
    DL_UART_enableInterrupt(UART_BLE_INST, DL_UART_INTERRUPT_RX);
}



/**
 * BLE_RxHandler
 * 字节同时喂给二进制协议层与 ASCII 行解析器。
 */
void BLE_RxHandler(uint8_t byte)
{
    BLE_Protocol_Feed(byte);        /* 二进制协议栈 */

    /* ASCII 行解析：以 \n 为分隔符。 */
    if (byte == '\n' || byte == '\r') {
        if (ascii_idx > 0) {
            ascii_buf[ascii_idx] = '\0';
            BLE_ParseAscii(ascii_buf);
            ascii_idx = 0;
        }
    } else if (byte >= 0x20 && byte < 0x7F && ascii_idx < 63) {
        ascii_buf[ascii_idx++] = (char)byte;
    } else {
        ascii_idx = 0;  /* 非 ASCII → 重置 */
    }
}

/**
 * BLE_ParseAscii — 解析 ASCII 命令
 * 格式: DRV,throttle,steer  /  STOP  /  PING  /  GET_HW
 */
void BLE_ParseAscii(const char *line)
{
    while (*line == ' ' || *line == '\n') line++;
    if (*line == '\0') return;

    /* S0/S1 到达解析层即刻关闭实时驱动与清空普通队列；不用等待下一次 10ms 主循环。
     * S1 的任务执行期间还会在 empty.c 每轮清零 L/R，防止任务结束后残留速度复活。 */
    if (strcmp(line, "EMG") == 0 || strcmp(line, "BRK") == 0 || strcmp(line, "STP") == 0 ||
        strcmp(line, "AT-") == 0 || strcmp(line, "LF-") == 0 ||
        strcmp(line, "CAL") == 0 || strcmp(line, "GCL") == 0 ||
        strcmp(line, "AT+") == 0 || strcmp(line, "LF+") == 0) {
        BLE_EnterSafetyLock();
    }

    /* L/R 差速直接存储：仅在未被 S0/S1 防抖窗口锁定时接受。 */
    if (line[0] == 'L' && line[3] == '\0' && ascii_is_hex(line[1]) && ascii_is_hex(line[2])) {
        if (!BLE_IsSafetyLocked()) {
            ble_left_val = (int)strtol(line+1, NULL, 16) - 128;  /* hex-128 → -100~100 */
            ble_drive_active = 1;
            ble_last_cmd_ms = mspm0_get_clock_ms();
        }
        return;
    }
    if (line[0] == 'R' && line[3] == '\0' && ascii_is_hex(line[1]) && ascii_is_hex(line[2])) {
        if (!BLE_IsSafetyLocked()) {
            ble_right_val = (int)strtol(line+1, NULL, 16) - 128;
            ble_drive_active = 1;
            ble_last_cmd_ms = mspm0_get_clock_ms();
        }
        return;
    }

    /* 其他命令入 FIFO，防止连续 PING/MST 覆盖较早的校准或整定命令。 */
    if (strcmp(line, "PNG") == 0) {
        BLE_QueueAsciiCmd("PING");
    } else if (strcmp(line, "STP") == 0) {
        BLE_QueueAsciiCmd("STOP");
    } else if (strcmp(line, "EMG") == 0) {
        BLE_QueueAsciiCmd("EMG");
    } else if (strcmp(line, "BRK") == 0) {
        BLE_QueueAsciiCmd("BRK");
    } else if (strcmp(line, "HW?") == 0) {
        BLE_QueueAsciiCmd("GET_HW");
    } else if (strcmp(line, "TM+") == 0) {
        /* 遥测启动必须幂等：重复点击/旧页面残包只保留一个启动请求。 */
        BLE_QueueAsciiCmd("TELEM_ON");
    } else if (strcmp(line, "TM-") == 0) {
        /* 停止优先级高于遥测启动，避免 TM+ 在停止之后重新生效。 */
        BLE_QueueAsciiCmd("TELEM_OFF");
    } else if (strcmp(line, "CAL") == 0) {
        BLE_QueueAsciiCmd("IMU_CAL");
    } else if (strcmp(line, "YZR") == 0) {
        BLE_QueueAsciiCmd("YAW_ZERO");
    } else if (strcmp(line, "MST") == 0) {
        BLE_QueueAsciiCmd("MOTOR_STATUS");
    } else if (strcmp(line, "LST") == 0) {
        BLE_QueueAsciiCmd("LINE_STATUS");
    } else if (strcmp(line, "GCL") == 0) {
        BLE_QueueAsciiCmd("GRAY_CAL");
    } else if (strcmp(line, "LF+") == 0) {
        BLE_QueueAsciiCmd("LINE_START");
    } else if (strcmp(line, "LF-") == 0) {
        BLE_QueueAsciiCmd("LINE_STOP");
    } else if (strcmp(line, "AT+") == 0) {
        BLE_QueueAsciiCmd("SPEED_TUNE_START");
    } else if (strcmp(line, "AT-") == 0) {
        BLE_QueueAsciiCmd("SPEED_TUNE_STOP");
    } else if (line[0] == 'P' && line[3] == '\0' &&
               (line[1] == 'P' || line[1] == 'D') &&
               ((line[2] >= '0' && line[2] <= '9') || (line[2] >= 'A' && line[2] <= 'F'))) {
        char adjust[4] = {'P', line[1], line[2], '\0'};
        BLE_QueueAsciiCmd(adjust);
    } else if (line[0] == 'B' && line[1] == 'Z' && line[3] == '\0' &&
               ((line[2] >= '0' && line[2] <= '9') || (line[2] >= 'A' && line[2] <= 'F'))) {
        char bias[4] = {'B', 'Z', line[2], '\0'};
        BLE_QueueAsciiCmd(bias);
    }
}

uint32_t  BLE_GetLastCmdMs(void) { return ble_last_cmd_ms; }

/* L/R 差速值读取接口 */
int       BLE_GetLeft(void)         { return ble_left_val; }
int       BLE_GetRight(void)        { return ble_right_val; }
int       BLE_GetDriveActive(void)  { return ble_drive_active; }
void      BLE_ResetDrive(void)      { ble_left_val = 0; ble_right_val = 0; ble_drive_active = 0; }
void      BLE_EnterSafetyLock(void)
{
    BLE_ClearAsciiQueue();
    BLE_ResetDrive();
    ble_safety_until_ms = mspm0_get_clock_ms() + BLE_SAFETY_GUARD_MS;
}
void      BLE_ClearSafetyLock(void) { ble_safety_until_ms = 0; }
uint8_t   BLE_IsSafetyLocked(void)
{
    return ble_safety_until_ms != 0U &&
           (int32_t)(ble_safety_until_ms - mspm0_get_clock_ms()) > 0;
}

/* ── 其他命令 FIFO 读取接口：每轮总是选出最高优先级命令。 ── */
int BLE_AsciiReady(void) { return ascii_cmd_count > 0U; }
const char* BLE_GetCmd(void)
{
    uint8_t i, index, best = ASCII_CMD_QUEUE_SIZE, best_prio = 0xFF;
    for (i = 0; i < ascii_cmd_count; i++) {
        index = (uint8_t)((ascii_cmd_head + i) % ASCII_CMD_QUEUE_SIZE);
        if (ascii_cmd_priority[index] < best_prio) {
            best_prio = ascii_cmd_priority[index]; best = index;
        }
    }
    ascii_cmd_selected = best;
    return best < ASCII_CMD_QUEUE_SIZE ? ascii_cmd_queue[best] : "";
}
void BLE_AsciiClear(void)
{
    uint8_t index, next;
    if (ascii_cmd_count == 0U || ascii_cmd_selected >= ASCII_CMD_QUEUE_SIZE) return;
    index = (uint8_t)((ascii_cmd_selected + ASCII_CMD_QUEUE_SIZE - ascii_cmd_head) % ASCII_CMD_QUEUE_SIZE);
    for (; (uint8_t)(index + 1U) < ascii_cmd_count; index++) {
        uint8_t dst = (uint8_t)((ascii_cmd_head + index) % ASCII_CMD_QUEUE_SIZE);
        next = (uint8_t)((ascii_cmd_head + index + 1U) % ASCII_CMD_QUEUE_SIZE);
        strcpy(ascii_cmd_queue[dst], ascii_cmd_queue[next]);
        ascii_cmd_priority[dst] = ascii_cmd_priority[next];
    }
    ascii_cmd_tail = (uint8_t)((ascii_cmd_tail + ASCII_CMD_QUEUE_SIZE - 1U) % ASCII_CMD_QUEUE_SIZE);
    ascii_cmd_count--;
    ascii_cmd_selected = ASCII_CMD_QUEUE_SIZE;
}

void BLE_SendByte(uint8_t data)
{
    (void)BLE_TxEnqueue(&data, 1U, BLE_TX_PRIO_CONTROL);
}

void BLE_SendString(const char *str)
{
    uint16_t len = 0;
    if (str == NULL) return;
    while (str[len] != '\0' && len < BLE_TX_FRAME_MAX) len++;
    (void)BLE_TxEnqueue((const uint8_t *)str, len, BLE_TX_PRIO_CONTROL);
}

void BLE_EnterATMode(void)  { BLE_SendString("AT...\r\n"); }
void BLE_ExitATMode(void)   { BLE_SendString("AT+EXIT\r\n"); }
