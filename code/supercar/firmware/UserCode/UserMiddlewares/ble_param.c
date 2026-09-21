/**
 * @file    ble_param.c
 * @brief   蓝牙参数表管理 — 实现
 *
 * 参数注册表: 通过 param_entry_t 结构体把参数 ID 绑定到实际变量指针。
 * 外部模块 (empty.c / car.c / app_imu.c) 的变量通过 extern 引用。
 * Flash 持久化存储在 Flash 末尾 2KB (0x0001F800)。
 */

#include "ble_param.h"
#include "pid.h"
#include "ti_msp_dl_config.h"
#include <math.h>
#include <string.h>

/* ---- 外部变量 (empty.c 定义) ---- */
extern PID_t pidLeftSpeed;
extern PID_t pidRightSpeed;
extern PID_t pidLine;
extern PID_t pidAngle;

/* ---- 运动参数 (暂无 car.c, 在此定义占位变量) ---- */
/* App 点动/遥控按百分比换算此值。解除原先 30cm/s 的软件限速，满输出可使用与 MCU 台架一致的 110cm/s 目标。 */
static float g_base_speed   = 110.0f;
static float g_turn_inner   = 10.0f;
static float g_turn_outer   = 25.0f;
static float g_turn_target  = 1344.0f;
static float g_soft_start   = 10.0f;
static float g_laps         = 1.0f;
static float g_gray_thresh  = 800.0f;

/* ---- IMU 零偏 (暂无暴露接口, 占位) ---- */
static float g_gyro_bias_x = 0.0f;
static float g_gyro_bias_y = 0.0f;
static float g_gyro_bias_z = 0.0f;

/* ---- 速度自适应 PID 曲线 ----
 * active 仅由控制环读取；pending 仅由 App 调试阶段写入。
 * Commit 由上层在安全静止条件下调用，避免 ISR 读取到半组参数。 */
static pid_schedule_node_t g_pid_schedule_active[PID_SCHEDULE_NODE_COUNT] = {
    /* speed, velocity PID, normal-line PID, curve-line PID + enter threshold, yaw, distance
     * 2026-07-29 三轮航向：bench_yaw90_40cms 汇总（20 已收敛微调，30/40 终值残差进 ±2°）。 */
    {20.0f, 22.1692f, 0.0000f, 5.4746f, 8.2071f, 0.00103f, 0.000015f, 13.00f, 0.00130f, 0.000022f, 18.00f, 1800.0f, 0.6155f, 0.1003f, 18.00f, 0.02500f, 2400.0f, 8.00f, 1.0100f},
    {30.0f, 49.2849f, 0.0000f, 0.8266f, 8.0268f, 0.00111f, 0.000020f, 19.00f, 0.00171f, 0.000028f, 27.00f, 1800.0f, 0.7267f, 0.1653f, 25.00f, 0.03000f, 2000.0f, 10.00f, 1.0030f},
    {40.0f, 51.6852f, 0.0000f, 1.1452f, 8.4670f, 0.00154f, 0.000027f, 27.00f, 0.00219f, 0.000036f, 38.00f, 1800.0f, 0.9471f, 0.2112f, 32.00f, 0.03500f, 2600.0f, 13.00f, 1.0000f},
};
static pid_schedule_node_t g_pid_schedule_pending[PID_SCHEDULE_NODE_COUNT] = {
    {20.0f, 22.1692f, 0.0000f, 5.4746f, 8.2071f, 0.00103f, 0.000015f, 13.00f, 0.00130f, 0.000022f, 18.00f, 1800.0f, 0.6155f, 0.1003f, 18.00f, 0.02500f, 2400.0f, 8.00f, 1.0100f},
    {30.0f, 49.2849f, 0.0000f, 0.8266f, 8.0268f, 0.00111f, 0.000020f, 19.00f, 0.00171f, 0.000028f, 27.00f, 1800.0f, 0.7267f, 0.1653f, 25.00f, 0.03000f, 2000.0f, 10.00f, 1.0030f},
    {40.0f, 51.6852f, 0.0000f, 1.1452f, 8.4670f, 0.00154f, 0.000027f, 27.00f, 0.00219f, 0.000036f, 38.00f, 1800.0f, 0.9471f, 0.2112f, 32.00f, 0.03500f, 2600.0f, 13.00f, 1.0000f},
};

/* ==================== 参数表 ==================== */
param_entry_t g_param_table[] = {
    /* PID — 速度环 */
    {0x00, "PID_SPEED_KP",  &pidLeftSpeed.kp,  60.0f,       0.0f,  200.0f},
    {0x01, "PID_SPEED_KI",  &pidLeftSpeed.ki,  0.0f,        0.0f,  50.0f},
    {0x02, "PID_SPEED_KD",  &pidLeftSpeed.kd,  0.5f,        0.0f,  20.0f},
    {0x03, "PID_SPEED_FF",  &pidLeftSpeed.kf,  8.0f,        0.0f,  50.0f},

    /* PID — 循迹环 */
    {0x04, "PID_LINE_KP",   &pidLine.kp,       0.00105055f, 0.0f,  0.01f},
    {0x05, "PID_LINE_KD",   &pidLine.kd,       0.0000199f,  0.0f,  0.001f},

    /* PID — 角度环 */
    {0x06, "PID_ANGLE_KP",  &pidAngle.kp,      0.165f,      0.0f,  5.0f},
    {0x07, "PID_ANGLE_KD",  &pidAngle.kd,      0.01f,       0.0f,  1.0f},

    /* 运动参数 */
    {0x08, "BASE_SPEED",    &g_base_speed,     110.0f,      0.0f,  150.0f},
    {0x09, "TURN_INNER",    &g_turn_inner,     10.0f,       0.0f,  40.0f},
    {0x0A, "TURN_OUTER",    &g_turn_outer,     25.0f,       0.0f,  60.0f},
    {0x0B, "TURN_TARGET",   &g_turn_target,    1344.0f,     0.0f,  5000.0f},
    {0x0C, "SOFT_START",    &g_soft_start,     10.0f,       0.0f,  30.0f},
    {0x0D, "LAPS",          &g_laps,           1.0f,        1.0f,  10.0f},
    {0x0E, "GRAY_THRESH",   &g_gray_thresh,    800.0f,      0.0f,  4095.0f},

    /* IMU */
    {0x10, "GYRO_BIAS_X",   &g_gyro_bias_x,    0.0f,      -500.0f, 500.0f},
    {0x11, "GYRO_BIAS_Y",   &g_gyro_bias_y,    0.0f,      -500.0f, 500.0f},
    {0x12, "GYRO_BIAS_Z",   &g_gyro_bias_z,    0.0f,      -500.0f, 500.0f},
};

const uint8_t g_param_count = sizeof(g_param_table) / sizeof(g_param_table[0]);

/* ==================== API 实现 ==================== */

void BLE_Param_Init(void)
{
    /* 参数表已在编译时初始化, 此处预留 Flash 加载入口 */
    /* BLE_Param_LoadFromFlash();  // 可选: 上电自动加载 */
}

float BLE_Param_Read(uint8_t id)
{
    uint8_t i;
    for (i = 0; i < g_param_count; i++) {
        if (g_param_table[i].id == id) {
            return *(g_param_table[i].ptr);
        }
    }
    return 0.0f;
}

bool BLE_Param_Write(uint8_t id, float value)
{
    uint8_t i;
    for (i = 0; i < g_param_count; i++) {
        if (g_param_table[i].id == id) {
            /* 限幅 */
            if (value > g_param_table[i].max_val)
                value = g_param_table[i].max_val;
            if (value < g_param_table[i].min_val)
                value = g_param_table[i].min_val;
            *(g_param_table[i].ptr) = value;

            /* 速度参数左右环同步 */
            if (id == 0x00) pidRightSpeed.kp = value;
            if (id == 0x01) pidRightSpeed.ki = value;
            if (id == 0x02) pidRightSpeed.kd = value;
            if (id == 0x03) pidRightSpeed.kf = value;
            return true;
        }
    }
    return false;
}

void BLE_Param_LoadDefaults(void)
{
    uint8_t i;
    for (i = 0; i < g_param_count; i++) {
        *(g_param_table[i].ptr) = g_param_table[i].def_val;
    }
    /* 同步右轮 PID */
    pidRightSpeed.kp = pidLeftSpeed.kp;
    pidRightSpeed.ki = pidLeftSpeed.ki;
    pidRightSpeed.kd = pidLeftSpeed.kd;
    pidRightSpeed.kf = pidLeftSpeed.kf;
}

bool BLE_Param_GetEntry(uint8_t index,
                        char *name_out, float *val_out,
                        float *def_out, float *min_out, float *max_out)
{
    if (index >= g_param_count) return false;

    const param_entry_t *e = &g_param_table[index];

    /* name: 最多 23 字符, 确保 '\0' 结尾 */
    strncpy(name_out, e->name, 23);
    name_out[23] = '\0';

    *val_out = *(e->ptr);
    *def_out = e->def_val;
    *min_out = e->min_val;
    *max_out = e->max_val;
    return true;
}

/* ==================== Flash 持久化 ==================== */

/*
 * Flash 布局:
 *   参数区: 0x0001F800 ~ 0x0001FFFF (2KB, 最后 2 个 sector)
 *   格式: [magic:u32 0xBEAF] [count:u8] [values:f32×count] [crc16:u16]
 *
 *   CRC-16 用于校验完整性。
 *   首次烧写或 Flash 为空时返回 0xFFFFFFFF → 视为无效, 使用默认值。
 */

#define FLASH_PARAM_BASE  0x0001F800
#define FLASH_PARAM_MAGIC 0xBEEFCAFE

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i, j;
    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/*
 * Flash 写入需要执行在 SRAM 中 (避免读 Flash 时写 Flash)。
 * __ramfunc 属性由 Keil 链接脚本处理, 将该函数放在 .ramfunc 段。
 */
#ifdef __CC_ARM
  #define RAMFUNC __attribute__((section(".ramfunc")))
#else
  #define RAMFUNC
#endif

RAMFUNC static int flash_param_do_save(void)
{
    /* MSPM0 Flash API requires unprotect → erase → program */
    /* This is a simplified placeholder. Full implementation needs:
     *   DL_FlashCTL_unprotectSector(FLASHCTL, FLASH_PARAM_BASE, DL_FLASHCTL_PROTECT_WRITE_ERASE);
     *   DL_FlashCTL_eraseMemory(FLASH_PARAM_BASE, DL_FLASHCTL_COMMAND_SIZE_SECTOR);
     *   DL_FlashCTL_programMemory(FLASH_PARAM_BASE, (uint32_t*)buffer, word_count);
     */
    return -1;
}

bool BLE_Param_SaveToFlash(void)
{
    /*
     * TODO: 实现完整的 Flash 写入。
     * 当前为占位实现, 参数保存在 RAM 中 (掉电丢失)。
     * 后续用 MSPM0 DriverLib Flash API 完善。
     */
    uint8_t buffer[256];
    uint8_t i;
    uint16_t offset = 0;
    float val;

    /* 序列化 */
    uint32_t magic = FLASH_PARAM_MAGIC;
    memcpy(&buffer[offset], &magic, 4); offset += 4;

    buffer[offset++] = (uint8_t)g_param_count;

    for (i = 0; i < g_param_count; i++) {
        val = *(g_param_table[i].ptr);
        memcpy(&buffer[offset], &val, 4);
        offset += 4;
    }

    uint16_t crc = crc16_ccitt(buffer, offset);
    memcpy(&buffer[offset], &crc, 2);
    offset += 2;

    /* Flash 写入 (占位) */
    (void)flash_param_do_save;
    (void)buffer;
    (void)offset;

    return false; /* 暂未实现 */
}

bool BLE_Param_LoadFromFlash(void)
{
    uint32_t *flash_ptr = (uint32_t *)FLASH_PARAM_BASE;

    /* 检查 magic 有效性 */
    if (flash_ptr[0] != FLASH_PARAM_MAGIC &&
        flash_ptr[0] != 0xFFFFFFFF) {
        /* Magic 不匹配且非空白, 使用默认值 */
        return false;
    }

    if (flash_ptr[0] == 0xFFFFFFFF) {
        /* Flash 空白 (未编程), 使用默认值 */
        return false;
    }

    /* Magic 匹配: 反序列化参数 */
    uint8_t count = ((uint8_t *)flash_ptr)[4];
    if (count != g_param_count) {
        return false; /* 参数数量变化, 不加载 */
    }

    uint8_t i;
    float *vals = (float *)(flash_ptr + 2); /* 跳过 magic + count */
    for (i = 0; i < count && i < g_param_count; i++) {
        *(g_param_table[i].ptr) = vals[i];
    }

    /* 同步右轮 PID */
    pidRightSpeed.kp = pidLeftSpeed.kp;
    pidRightSpeed.ki = pidLeftSpeed.ki;
    pidRightSpeed.kd = pidLeftSpeed.kd;
    pidRightSpeed.kf = pidLeftSpeed.kf;

    return true;
}

/* 导出运动参数供外部使用 */
float BLE_Param_GetBaseSpeed(void)   { return g_base_speed; }
float BLE_Param_GetTurnInner(void)   { return g_turn_inner; }
float BLE_Param_GetTurnOuter(void)   { return g_turn_outer; }
float BLE_Param_GetTurnTarget(void)  { return g_turn_target; }
float BLE_Param_GetSoftStart(void)   { return g_soft_start; }
float BLE_Param_GetLaps(void)        { return g_laps; }
float BLE_Param_GetGrayThresh(void)  { return g_gray_thresh; }

const pid_schedule_node_t *BLE_Param_GetPidSchedule(void)
{
    return g_pid_schedule_active;
}

const pid_schedule_node_t *BLE_Param_GetPidSchedulePending(void)
{
    return g_pid_schedule_pending;
}

static bool PID_ScheduleNodeIsValid(uint8_t index, const pid_schedule_node_t *node)
{
    if (node == 0 || index >= PID_SCHEDULE_NODE_COUNT) return false;
    if (!isfinite(node->speed_cm_s) || !isfinite(node->speed_kp) ||
        !isfinite(node->speed_ki) || !isfinite(node->speed_kd) ||
        !isfinite(node->speed_kf) || !isfinite(node->line_kp) ||
        !isfinite(node->line_kd) || !isfinite(node->line_max_steer) ||
        !isfinite(node->curve_kp) || !isfinite(node->curve_kd) ||
        !isfinite(node->curve_max_steer) || !isfinite(node->curve_error_trigger) ||
        !isfinite(node->yaw_kp) || !isfinite(node->yaw_kd) ||
        !isfinite(node->yaw_max_steer) || !isfinite(node->pos_kp) ||
        !isfinite(node->decel_pulses) || !isfinite(node->end_speed) ||
        !isfinite(node->distance_scale)) return false;
    if (node->speed_cm_s < 5.0f || node->speed_cm_s > 80.0f) return false;
    if (node->speed_kp < 0.0f || node->speed_kp > 200.0f ||
        node->speed_ki < 0.0f || node->speed_ki > 50.0f ||
        node->speed_kd < 0.0f || node->speed_kd > 20.0f ||
        node->speed_kf < 0.0f || node->speed_kf > 50.0f) return false;
    if (node->line_kp < 0.0f || node->line_kp > 0.01f ||
        node->line_kd < 0.0f || node->line_kd > 0.001f ||
        node->line_max_steer < 1.0f || node->line_max_steer > 60.0f) return false;
    if (node->curve_kp < 0.0f || node->curve_kp > 0.02f ||
        node->curve_kd < 0.0f || node->curve_kd > 0.002f ||
        node->curve_max_steer < 1.0f || node->curve_max_steer > 80.0f ||
        node->curve_error_trigger < 128.0f || node->curve_error_trigger > 6000.0f) return false;
    if (node->yaw_kp < 0.0f || node->yaw_kp > 5.0f ||
        node->yaw_kd < 0.0f || node->yaw_kd > 1.0f ||
        node->yaw_max_steer < 1.0f || node->yaw_max_steer > 60.0f) return false;
    if (node->pos_kp < 0.0f || node->pos_kp > 0.2f ||
        node->decel_pulses < 100.0f || node->decel_pulses > 6000.0f ||
        node->end_speed < 1.0f || node->end_speed > 40.0f ||
        node->distance_scale < 0.80f || node->distance_scale > 1.20f) return false;
    return true;
}

bool BLE_Param_WritePidScheduleNode(uint8_t index, const pid_schedule_node_t *node)
{
    if (!PID_ScheduleNodeIsValid(index, node)) return false;
    g_pid_schedule_pending[index] = *node;
    return true;
}

bool BLE_Param_CommitPidSchedule(void)
{
    uint8_t i;
    uint32_t primask;
    for (i = 0; i < PID_SCHEDULE_NODE_COUNT; i++) {
        if (!PID_ScheduleNodeIsValid(i, &g_pid_schedule_pending[i])) return false;
        if (i > 0 && g_pid_schedule_pending[i].speed_cm_s <=
                     g_pid_schedule_pending[i - 1].speed_cm_s) return false;
    }

    /* TIMA0 ISR 会在 10ms 周期读取 active 表。短临界区内完整复制，
     * 确保 ISR 只能看到旧曲线或新曲线，绝不读取半组节点。 */
    primask = __get_PRIMASK();
    __disable_irq();
    memcpy(g_pid_schedule_active, g_pid_schedule_pending, sizeof(g_pid_schedule_active));
    __set_PRIMASK(primask);
    return true;
}

void BLE_Param_ResetPidSchedulePending(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    memcpy(g_pid_schedule_pending, g_pid_schedule_active, sizeof(g_pid_schedule_active));
    __set_PRIMASK(primask);
}

bool BLE_Param_AutoTuneSpeedNode(float test_speed_cm_s, float kp, float ki, float kd, float kf)
{
    uint8_t i, nearest = 0U;
    float bestDiff = 10000.0f;
    for (i = 0; i < PID_SCHEDULE_NODE_COUNT; i++) {
        float diff = g_pid_schedule_pending[i].speed_cm_s - test_speed_cm_s;
        if (diff < 0.0f) diff = -diff;
        if (diff < bestDiff) { bestDiff = diff; nearest = i; }
    }
    g_pid_schedule_pending[nearest].speed_kp = kp;
    g_pid_schedule_pending[nearest].speed_ki = ki;
    g_pid_schedule_pending[nearest].speed_kd = kd;
    g_pid_schedule_pending[nearest].speed_kf = kf;
    return BLE_Param_CommitPidSchedule();
}

static float PID_Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

void BLE_Param_InterpolatePidSchedule(float speed_cm_s, pid_schedule_node_t *out)
{
    const pid_schedule_node_t *a = &g_pid_schedule_active[0];
    const pid_schedule_node_t *b = &g_pid_schedule_active[PID_SCHEDULE_NODE_COUNT - 1];
    uint8_t i;
    float t;
    if (out == 0) return;
    if (speed_cm_s <= a->speed_cm_s) { *out = *a; return; }
    if (speed_cm_s >= b->speed_cm_s) { *out = *b; return; }
    for (i = 0; i + 1U < PID_SCHEDULE_NODE_COUNT; i++) {
        if (speed_cm_s <= g_pid_schedule_active[i + 1U].speed_cm_s) {
            a = &g_pid_schedule_active[i];
            b = &g_pid_schedule_active[i + 1U];
            break;
        }
    }
    t = (speed_cm_s - a->speed_cm_s) / (b->speed_cm_s - a->speed_cm_s);
    *out = *a;
    out->speed_cm_s = speed_cm_s;
    out->speed_kp = PID_Lerp(a->speed_kp, b->speed_kp, t);
    out->speed_ki = PID_Lerp(a->speed_ki, b->speed_ki, t);
    out->speed_kd = PID_Lerp(a->speed_kd, b->speed_kd, t);
    out->speed_kf = PID_Lerp(a->speed_kf, b->speed_kf, t);
    out->line_kp = PID_Lerp(a->line_kp, b->line_kp, t);
    out->line_kd = PID_Lerp(a->line_kd, b->line_kd, t);
    out->line_max_steer = PID_Lerp(a->line_max_steer, b->line_max_steer, t);
    out->curve_kp = PID_Lerp(a->curve_kp, b->curve_kp, t);
    out->curve_kd = PID_Lerp(a->curve_kd, b->curve_kd, t);
    out->curve_max_steer = PID_Lerp(a->curve_max_steer, b->curve_max_steer, t);
    out->curve_error_trigger = PID_Lerp(a->curve_error_trigger, b->curve_error_trigger, t);
    out->yaw_kp = PID_Lerp(a->yaw_kp, b->yaw_kp, t);
    out->yaw_kd = PID_Lerp(a->yaw_kd, b->yaw_kd, t);
    out->yaw_max_steer = PID_Lerp(a->yaw_max_steer, b->yaw_max_steer, t);
    out->pos_kp = PID_Lerp(a->pos_kp, b->pos_kp, t);
    out->decel_pulses = PID_Lerp(a->decel_pulses, b->decel_pulses, t);
    out->end_speed = PID_Lerp(a->end_speed, b->end_speed, t);
    out->distance_scale = PID_Lerp(a->distance_scale, b->distance_scale, t);
}
