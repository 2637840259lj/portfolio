/**
 * @file app_template.c
 * @brief 通用底层闭环台架测试：循迹、90度航向、1m距离停车。
 * @note 不包含、不依赖任何竞赛状态机；所有控制均复用 MCU 本地 10ms 速度内环。
 */
#include "app_template.h"
#include "app_ble.h"
#include "app_imu.h"
#include "at4950.h"
#include "ble.h"
#include "ble_param.h"
#include "ble_protocol.h"
#include "encoder_driver.h"
#include "No_Mcu_Ganv_Grayscale_Sensor.h"
#include "pid.h"
#include "sys_time.h"
#include <math.h>
#include <string.h>

extern MOTOR_t motorLeft;
extern MOTOR_t motorRight;
extern Encoder_t encoderLeft;
extern Encoder_t encoderRight;
extern PID_t pidLeftSpeed;
extern PID_t pidRightSpeed;
extern PID_t pidLine;
extern PID_t pidAngle;
extern PID_t pidPosition;
extern float targetSpeedLeft;
extern float targetSpeedRight;
extern No_MCU_Sensor g_GraySensor;
extern uint8_t g_gray_runtime_calibrated;
extern uint8_t Get_Gray_Digital(void);

#define BENCH_LINE_LOST_MS       250U
#define BENCH_LINE_TIMEOUT_MS  30000U
/* 航向到位过严会导致车已停住仍一直出「安全超时」。允许略大残差与
 * 更长窗口；超时瞬间若仍在可用偏差内，按达到目标收口并回传真实 finalYaw。 */
#define BENCH_TURN_TIMEOUT_MS  12000U
#define BENCH_TURN_SETTLE_MS     250U
#define BENCH_TURN_OK_ERR_DEG      3.5f
#define BENCH_TURN_OK_RATE_DPS    18.0f
#define BENCH_TURN_SOFT_OK_DEG    10.0f
#define BENCH_TARGET_YAW_DEG     90.0f
#define BENCH_TARGET_DIST_CM    100.0f
#define BENCH_DIST_TIMEOUT_MS  15000U
#define BENCH_DIST_TOLERANCE_CM   0.5f
#define BENCH_DIST_SETTLE_MS      150U
#define BENCH_DIST_STOP_SPEED      3.0f
#define BENCH_PULSES_PER_CM (ENCODER_PPR / WHEEL_CIRCUMFERENCE_CM)

typedef struct {
    app_bench_mode_t mode;
    uint8_t node_index;
    uint32_t started_ms;
    uint32_t line_lost_since_ms;
    uint32_t yaw_in_band_since_ms;
    uint32_t distance_in_band_since_ms;
    int32_t start_left;
    int32_t start_right;
    float start_yaw;
    float peak_abs_yaw;
    float peak_abs_error;
    float sum_abs_error;
    float sum_abs_steer;
    uint16_t samples;
    uint16_t curve_samples;
    pid_schedule_node_t node;
} bench_state_t;

static bench_state_t s_bench;

static void write_i16(uint8_t *d, uint8_t off, int16_t v)
{
    d[off] = (uint8_t)(v & 0xFF);
    d[off + 1U] = (uint8_t)((v >> 8) & 0xFF);
}

static void write_i32(uint8_t *d, uint8_t off, int32_t v)
{
    d[off] = (uint8_t)(v & 0xFF);
    d[off + 1U] = (uint8_t)((v >> 8) & 0xFF);
    d[off + 2U] = (uint8_t)((v >> 16) & 0xFF);
    d[off + 3U] = (uint8_t)((v >> 24) & 0xFF);
}

static float clampf(float value, float lo, float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

/* 相对航向必须基于本次测试开始时的姿态，且避免越过 +/-180° 时误算成接近360°。 */
static float relative_yaw_deg(float current, float start)
{
    float value = current - start;
    while (value > 180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

/* 白底黑线的 `Digtal` 经滞回处理：探头离线后若灰度长期停在黑/白阈值之间，
 * 数字位可能保留上一次“压线”状态。因此台架安全停止不能只看位图，
 * 必须用当周期的归一化模拟量重新判断是否仍存在足够黑线信号。
 * 正规化值接近 0 表示黑，接近 bits 表示白；任一通道的黑线强度超过
 * 满量程 1/3 即视为仍在线上，和 CalculateNormalizedValue 的有效信号门限一致。 */
static bool bench_line_signal_present(void)
{
    uint8_t i;
    uint16_t bits;
    uint16_t signal_threshold;

    if (!g_GraySensor.ok || g_GraySensor.bits < 16.0) return false;
    bits = (uint16_t)g_GraySensor.bits;
    signal_threshold = bits / 3U;
    for (i = 0U; i < 8U; ++i) {
        uint16_t normal = g_GraySensor.Normal_value[i];
        uint16_t black_strength = normal >= bits ? 0U : (uint16_t)(bits - normal);
        if (black_strength > signal_threshold) return true;
    }
    return false;
}

static float average_distance_cm(void)
{
    float left = (float)(encoderLeft.total_count - s_bench.start_left);
    float right = (float)(encoderRight.total_count - s_bench.start_right);
    if (left < 0.0f) left = -left;
    if (right < 0.0f) right = -right;
    return (((left + right) * 0.5f) / BENCH_PULSES_PER_CM) * s_bench.node.distance_scale;
}

static void set_speed_targets(float left, float right)
{
    targetSpeedLeft = left;
    targetSpeedRight = right;
    PID_SetTarget(&pidLeftSpeed, left);
    PID_SetTarget(&pidRightSpeed, right);
}

static void stop_motors(void)
{
    BLE_ResetDrive();
    set_speed_targets(0.0f, 0.0f);
    PID_Reset(&pidLine);
    PID_Reset(&pidAngle);
    PID_Reset(&pidPosition);
    PID_Reset(&pidLeftSpeed);
    PID_Reset(&pidRightSpeed);
    Motor_Brake(&motorLeft);
    Motor_Brake(&motorRight);
}

static void send_result(uint8_t reason)
{
    uint8_t data[26];
    uint32_t elapsed = mspm0_get_clock_ms() - s_bench.started_ms;
    float distance = average_distance_cm();
    float final_yaw = relative_yaw_deg(g_imuData.yaw, s_bench.start_yaw);
    float mean_error = s_bench.samples == 0U ? 0.0f : s_bench.sum_abs_error / (float)s_bench.samples;
    float mean_steer = s_bench.samples == 0U ? 0.0f : s_bench.sum_abs_steer / (float)s_bench.samples;

    /* mode:u8 node:u8 reason:u8 curveSampleRatio:u8 elapsed:u32 distance:mm(i32)
     * finalYaw/peakYaw/meanErr: degree-or-line-unit *100(i16), meanSteer*10(i16), samples:u16 */
    data[0] = (uint8_t)s_bench.mode;
    data[1] = s_bench.node_index;
    data[2] = reason;
    data[3] = s_bench.samples == 0U ? 0U :
              (uint8_t)((uint32_t)s_bench.curve_samples * 100U / s_bench.samples);
    write_i32(data, 4, (int32_t)elapsed);
    write_i32(data, 8, (int32_t)(distance * 10.0f));
    write_i16(data, 12, (int16_t)(final_yaw * 100.0f));
    write_i16(data, 14, (int16_t)(s_bench.peak_abs_yaw * 100.0f));
    /* 均值误差可能超过 int16 按0.01缩放后的可表达范围；限制到327.66而不是
     * 327.67，后者乘100为32767，写入有符号int16会变为负值。 */
    write_i16(data, 16, (int16_t)(clampf(mean_error, 0.0f, 327.66f) * 100.0f));
    write_i16(data, 18, (int16_t)(mean_steer * 10.0f));
    write_i16(data, 20, (int16_t)s_bench.samples);
    write_i16(data, 22, (int16_t)(s_bench.node.line_max_steer * 10.0f));
    write_i16(data, 24, (int16_t)(s_bench.node.yaw_max_steer * 10.0f));
    BLE_Protocol_Send(BLE_CMD_BENCH_RESULT, data, sizeof(data));
}

static void finish_bench(uint8_t reason)
{
    app_bench_mode_t done_mode = s_bench.mode;
    stop_motors();
    if (g_ble_state == BLE_STATE_RUNNING) g_ble_state = BLE_STATE_IDLE;
    send_result(reason);
    memset(&s_bench, 0, sizeof(s_bench));
    s_bench.mode = APP_BENCH_NONE;
    (void)done_mode;
}

void AppTemplate_Init(void)
{
    memset(&s_bench, 0, sizeof(s_bench));
    s_bench.mode = APP_BENCH_NONE;
}

bool AppBench_IsActive(void)
{
    return s_bench.mode != APP_BENCH_NONE;
}

void AppBench_Abort(void)
{
    if (!AppBench_IsActive()) return;
    stop_motors();
    if (g_ble_state == BLE_STATE_RUNNING) g_ble_state = BLE_STATE_IDLE;
    memset(&s_bench, 0, sizeof(s_bench));
    s_bench.mode = APP_BENCH_NONE;
}

bool AppBench_Start(app_bench_mode_t mode, uint8_t node_index)
{
    const pid_schedule_node_t *nodes;
    if (mode < APP_BENCH_LINE_STRAIGHT || mode > APP_BENCH_LINE_CURVE) return false;
    if (node_index >= PID_SCHEDULE_NODE_COUNT || AppBench_IsActive()) return false;
    if (g_ble_remote_active || g_ble_state != BLE_STATE_IDLE) return false;
    if (fabsf(targetSpeedLeft) > 0.1f || fabsf(targetSpeedRight) > 0.1f) return false;
    /* 传感器硬件存在并不足以证明本次测评有效：对应类型必须完成显式校准。
     * 该门禁在 MCU 执行，旧 App 或直发 BLE 包不能绕过。 */
    if (!g_imuData.hw_ok) return false;
    if ((mode == APP_BENCH_LINE_STRAIGHT || mode == APP_BENCH_LINE_CURVE) && !g_gray_runtime_calibrated) return false;
    if ((mode == APP_BENCH_YAW90 || mode == APP_BENCH_DISTANCE_1M) && !ImuApp_HasRuntimeCalibration()) return false;

    nodes = BLE_Param_GetPidSchedule();
    memset(&s_bench, 0, sizeof(s_bench));
    s_bench.mode = mode;
    s_bench.node_index = node_index;
    s_bench.node = nodes[node_index];
    s_bench.started_ms = mspm0_get_clock_ms();
    s_bench.start_left = encoderLeft.total_count;
    s_bench.start_right = encoderRight.total_count;
    s_bench.start_yaw = g_imuData.yaw;

    PID_Reset(&pidLine);
    PID_Reset(&pidAngle);
    PID_Reset(&pidPosition);
    /* 直线和曲线是两条独立测试：只装载本模式对应的一组 PID，不做运行期混合。 */
    if (mode == APP_BENCH_LINE_CURVE) {
        pidLine.kp = s_bench.node.curve_kp;
        pidLine.kd = s_bench.node.curve_kd;
        pidLine.output_limit_p = s_bench.node.curve_max_steer;
        pidLine.output_limit_n = -s_bench.node.curve_max_steer;
    } else {
        pidLine.kp = s_bench.node.line_kp;
        pidLine.kd = s_bench.node.line_kd;
        pidLine.output_limit_p = s_bench.node.line_max_steer;
        pidLine.output_limit_n = -s_bench.node.line_max_steer;
    }
    pidAngle.kp = s_bench.node.yaw_kp;
    pidAngle.kd = s_bench.node.yaw_kd;
    pidAngle.output_limit_p = s_bench.node.yaw_max_steer;
    pidAngle.output_limit_n = -s_bench.node.yaw_max_steer;
    /* 位置外环的测量量是“剩余编码器脉冲”，目标为0；输出为基准速度。
     * 仅P项可避免停车段积分累积后再次推车，最大输出受当前速度节点限制。 */
    pidPosition.kp = s_bench.node.pos_kp;
    pidPosition.ki = 0.0f;
    pidPosition.kd = 0.0f;
    pidPosition.output_limit_p = s_bench.node.speed_cm_s;
    pidPosition.output_limit_n = 0.0f;
    PID_SetTarget(&pidPosition, 0.0f);
    PID_SetTarget(&pidLine, 0.0f);
    PID_SetTarget(&pidAngle, mode == APP_BENCH_YAW90 ? BENCH_TARGET_YAW_DEG : 0.0f);
    /* 不复位全局 yaw：测试结果始终相对本次启动姿态，与绝对初始角度无关。 */
    g_ble_state = BLE_STATE_RUNNING;
    return true;
}

void AppBench_Task(void)
{
    uint32_t now;
    uint32_t elapsed;
    float error;
    float steer;
    float yaw;
    float speed;
    float distance;
    float remaining_pulses;

    if (!AppBench_IsActive()) return;
    now = mspm0_get_clock_ms();
    elapsed = now - s_bench.started_ms;
    yaw = relative_yaw_deg(g_imuData.yaw, s_bench.start_yaw);
    if (fabsf(yaw) > s_bench.peak_abs_yaw) s_bench.peak_abs_yaw = fabsf(yaw);

    if (s_bench.mode == APP_BENCH_LINE_STRAIGHT || s_bench.mode == APP_BENCH_LINE_CURVE) {
        if (!bench_line_signal_present()) {
            if (s_bench.line_lost_since_ms == 0U) s_bench.line_lost_since_ms = now;
            if (now - s_bench.line_lost_since_ms >= BENCH_LINE_LOST_MS) {
                finish_bench(BLE_BENCH_REASON_LINE_LOST);
                return;
            }
            set_speed_targets(0.0f, 0.0f);
            return;
        }
        s_bench.line_lost_since_ms = 0U;
        error = -CalculateNormalizedValue(&g_GraySensor, 0U);
        /* 两个循迹模式各自只消费其独立 PID。曲线测试的全量样本标记为曲线段，
         * 便于 App 的会话统计和 CSV 审计，不再用阈值混入直线参数。 */
        if (s_bench.mode == APP_BENCH_LINE_CURVE) s_bench.curve_samples++;
        steer = PID_Calc(&pidLine, error);
        if (fabsf(error) > s_bench.peak_abs_error) s_bench.peak_abs_error = fabsf(error);
        s_bench.sum_abs_error += fabsf(error);
        s_bench.sum_abs_steer += fabsf(steer);
        s_bench.samples++;
        set_speed_targets(s_bench.node.speed_cm_s - steer, s_bench.node.speed_cm_s + steer);
        if (elapsed >= BENCH_LINE_TIMEOUT_MS) finish_bench(BLE_BENCH_REASON_TIMEOUT);
        return;
    }

    if (s_bench.mode == APP_BENCH_YAW90) {
        error = BENCH_TARGET_YAW_DEG - yaw;
        /* pidAngle 的目标已设置为90°，输入为相对 yaw。 */
        steer = PID_Calc(&pidAngle, yaw);
        s_bench.sum_abs_error += fabsf(error);
        s_bench.sum_abs_steer += fabsf(steer);
        s_bench.samples++;
        /* 已进入到位窗口时锁零目标，避免小残差仍持续差速抖动导致永远不静。 */
        if (fabsf(error) <= BENCH_TURN_OK_ERR_DEG && fabsf(g_imuData.yawRate) <= BENCH_TURN_OK_RATE_DPS) {
            set_speed_targets(0.0f, 0.0f);
            if (s_bench.yaw_in_band_since_ms == 0U) s_bench.yaw_in_band_since_ms = now;
            if (now - s_bench.yaw_in_band_since_ms >= BENCH_TURN_SETTLE_MS) {
                finish_bench(BLE_BENCH_REASON_TARGET_REACHED);
                return;
            }
        } else {
            s_bench.yaw_in_band_since_ms = 0U;
            set_speed_targets(-steer, steer);
        }
        if (elapsed >= BENCH_TURN_TIMEOUT_MS) {
            /* 已基本转到并停住：允许用真实角度作为“达到目标”，不再因 2°/速率门禁错过样本。 */
            if (fabsf(error) <= BENCH_TURN_SOFT_OK_DEG) {
                set_speed_targets(0.0f, 0.0f);
                finish_bench(BLE_BENCH_REASON_TARGET_REACHED);
            } else {
                finish_bench(BLE_BENCH_REASON_TIMEOUT);
            }
        }
        return;
    }

    distance = average_distance_cm();
    remaining_pulses = (BENCH_TARGET_DIST_CM - distance) * BENCH_PULSES_PER_CM;
    /* 到点不只看单次越界：抵达目标前0.5cm即清零速度目标，确认两轮速度
     * 已低于阈值并持续150ms才报告完成。这样即使有惯性越界，也不会在越界后
     * 被末速下限再次推向前方。 */
    if (distance >= BENCH_TARGET_DIST_CM - BENCH_DIST_TOLERANCE_CM) {
        float left_speed = fabsf(Encoder_GetSpeedLine(&encoderLeft));
        float right_speed = fabsf(Encoder_GetSpeedLine(&encoderRight));
        set_speed_targets(0.0f, 0.0f);
        if (left_speed <= BENCH_DIST_STOP_SPEED && right_speed <= BENCH_DIST_STOP_SPEED) {
            if (s_bench.distance_in_band_since_ms == 0U) s_bench.distance_in_band_since_ms = now;
            if (now - s_bench.distance_in_band_since_ms >= BENCH_DIST_SETTLE_MS) {
                finish_bench(BLE_BENCH_REASON_TARGET_REACHED);
                return;
            }
        } else {
            s_bench.distance_in_band_since_ms = 0U;
        }
        if (elapsed >= BENCH_DIST_TIMEOUT_MS) finish_bench(BLE_BENCH_REASON_TIMEOUT);
        return;
    }
    s_bench.distance_in_band_since_ms = 0U;
    /* 位置外环：目标剩余脉冲=0，输入=-remaining_pulses，使正的剩余距离产生正的
     * 基准速度命令。它不会直接写 PWM，而是给后级航向差速环和左右速度内环分配速度。 */
    PID_SetTarget(&pidPosition, 0.0f);
    speed = PID_Calc(&pidPosition, -remaining_pulses);
    speed = clampf(speed, 0.0f, s_bench.node.speed_cm_s);

    /* 减速剖面仍是安全上限：位置环给出“还应跑多快”，剖面保证接近目标时不会
     * 以巡航速度冲入终点。两者取较小值，再以 end_speed 克服低速静摩擦直到到点。 */
    if (remaining_pulses < s_bench.node.decel_pulses) {
        float ratio = clampf(remaining_pulses / s_bench.node.decel_pulses, 0.0f, 1.0f);
        float profile_speed = s_bench.node.end_speed +
                              (s_bench.node.speed_cm_s - s_bench.node.end_speed) * ratio;
        if (speed > profile_speed) speed = profile_speed;
    }
    if (speed < s_bench.node.end_speed) speed = s_bench.node.end_speed;

    error = -yaw;
    /* 1m直行保持起始航向0°。低速末段的差速不能大于当前基准速度，
     * 否则一个轮会被反向指令，绝对值里程会被错误累加。 */
    PID_SetTarget(&pidAngle, 0.0f);
    steer = PID_Calc(&pidAngle, yaw);
    steer = clampf(steer, -speed, speed);
    s_bench.sum_abs_error += fabsf(error);
    s_bench.sum_abs_steer += fabsf(steer);
    s_bench.samples++;
    set_speed_targets(speed - steer, speed + steer);
    if (elapsed >= BENCH_DIST_TIMEOUT_MS) finish_bench(BLE_BENCH_REASON_TIMEOUT);
}

void AppTemplate_Task(void)
{
    /* 台架测试推进由 empty.c 的 10ms 时基调用 AppBench_Task。 */
}
