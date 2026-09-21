/**
 * @file      empty.c
 * @brief     MSPM0G3507 智能小车 — 通用模板
 * @details
 *   初始化所有硬件外设和驱动模块，主循环只做 IO 维护，不包含任何任务逻辑。
 *   使用时复制整个项目，然后在 app_template.c 中实现你自己的功能。
 *
 *   已初始化的硬件:
 *     - 电机 (AT4950 PWM 双路)
 *     - 编码器 (13线磁编码器, 4倍频, GPIO 中断)
 *     - IMU  (MPU6050, I2C0, Mahony 姿态解算)
 *     - 按键 (3个, ebtn 库消抖)
 * 
 - LED  (RGB 三色)
 *     - 蜂鸣器
 *     - 灰度传感器 (8路, CD4051 多路复用, ADC)
 *     - UART (115200, printf 重定向)
 *     - BLE  (CH9141, UART2/PB12-PB13, 蓝牙透传遥控)
 *     - PID 控制器库
 *
 *   ISR:
 *     - TIMA0 10ms: 速度 PID 闭环 (在 isr.c 中执行)
 *     - GPIO 编码器中断
 *     - SysTick 1ms
 */
#include "ti_msp_dl_config.h"
#include "pid.h"
#include "bsp_adc.h"
#include "sys_time.h"
#include "No_Mcu_Ganv_Grayscale_Sensor.h"
#include "app_indicator.h"
#include "app_key.h"
#include "app_template.h"
#include "at4950.h"
#include "bsp_uart.h"
#include "buzzer.h"
#include "encoder_driver.h"
#include "led.h"
#include "app_imu.h"
#include "ble.h"
#include "ble_protocol.h"
#include "app_ble.h"
#include "app_page.h"
#include "ble_param.h"
#include "app_teach.h"
#include "app_ins.h"
#include "app_competition.h"
#include "app_ble_ascii.h"
#include "app_ble_remote.h"
#include "app_oled.h"
#include "ble_debug.h"
#include "app_ball_vision.h"
#include "app_ball_pd_monitor.h"
#include "app_ball_task3.h"
#include "app_stepper_test.h"
#include "tmc2209_driver.h"
#include <stdio.h>
#include <math.h>

/* 固定 4 字节 ASCII 请求的二进制诊断回传序列化。 */
static void write_i16_le(uint8_t *d, uint8_t off, int16_t v)
{
    d[off] = (uint8_t)(v & 0xFF); d[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}
static void write_i32_le(uint8_t *d, uint8_t off, int32_t v)
{
    d[off] = (uint8_t)(v & 0xFF); d[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    d[off + 2] = (uint8_t)((v >> 16) & 0xFF); d[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

/* ==================== 硬件对象 ==================== */
MOTOR_t   motorLeft;
MOTOR_t   motorRight;
Encoder_t encoderLeft;
Encoder_t encoderRight;

/* ==================== PID 对象 ==================== */
PID_t pidLeftSpeed;
PID_t pidRightSpeed;
PID_t pidLine;
PID_t pidAngle;
/* 1m 距离台架：位置外环输出基准速度，后续再由航向环和双轮速度环执行。 */
PID_t pidPosition;

/* ==================== 全局速度目标 (ISR 读取) ==================== */
float targetSpeedLeft  = 0.0f;
float targetSpeedRight = 0.0f;

/* K230 UART2 接收诊断默认关闭；仅在排查通信问题时临时打开。 */
#define BALL_VISION_DEBUG_PRINT 0
#define BALL_VISION_DEBUG_PERIOD_MS 1000U
/* PD 控制诊断默认关闭；仅在调参时临时打开。 */
#define BALL_PD_MONITOR_PRINT 0
#define BALL_PD_MONITOR_PERIOD_MS 1000U

/* 运动滚球补偿在10ms应用层从实际编码器速度构造，绝不进入车轮速度PID ISR。
 * 加速度先低通，避免编码器量化噪声直接驱动摆杆。 */
#define BALL_MOTION_ACCEL_FILTER_ALPHA 0.25f
static float s_ball_prev_vehicle_speed_cm_s = 0.0f;
static float s_ball_vehicle_accel_cm_s2 = 0.0f;

/* BLE 遥控 ramp 状态已封装到 app_ble_remote.c, 通过 AppBLE_RemoteReset() 清零 */

/* ==================== 速度闭环自动整定 ====================
 * AT+ 启动一次安全阶跃试验：双轮 35cm/s、最长4.5秒；AT-/BRK/EMG 可立即中止。
 * 结果按阶跃的稳态误差、超调和波动对 Kp/Kd/Kf 做小步调整，避免一次调参过大。 */
static bool     g_speed_tune_active = false;
static uint32_t g_speed_tune_start_ms = 0;
static float    g_speed_tune_sum_l = 0.0f, g_speed_tune_sum_r = 0.0f;
static float    g_speed_tune_max_l = 0.0f, g_speed_tune_max_r = 0.0f;
static float    g_speed_tune_min_l = 10000.0f, g_speed_tune_min_r = 10000.0f;
static uint16_t g_speed_tune_samples = 0;
/* App 仅可在停车调试状态设置下一轮阶跃速度；整定执行仍由 MCU 主循环独占。 */
float g_speed_tune_target = 35.0f;
#define SPEED_TUNE_SETTLE_MS 1500U
#define SPEED_TUNE_TOTAL_MS 4500U

/* 启动门禁依赖的独占任务标志：必须在 AppSpeedTune_Start 之前定义
 * （二者在下方循迹台架/白底校准段落使用，此处仅前置声明）。
 * 这两个标志已导出供 app_ble_ascii.c 检查独占任务状态。 */
bool g_line_debug_active = false;
bool g_gray_cal_active = false;
/* H题App离线采样会话标记；任何安全停止都会清空，避免取消后误回传旧结果。 */
static uint8_t g_h1_capture_kind = 0U;
static uint8_t g_h1_capture_method = 0U;

bool AppSpeedTune_Start(float target_speed_cm_s)
{
    /* 二进制整定命令必须真正启动同一条安全阶跃路径，不能只写目标速度后
     * 依赖第二条 ASCII AT+；否则 App 会收到 0x38 ACK 却看似“点击无反应”。 */
    if (g_speed_tune_active || AppBench_IsActive() || ImuApp_IsCalibrating() ||
        g_gray_cal_active || g_line_debug_active || g_ble_remote_active ||
        targetSpeedLeft > 0.1f || targetSpeedLeft < -0.1f ||
        targetSpeedRight > 0.1f || targetSpeedRight < -0.1f ||
        target_speed_cm_s < 10.0f || target_speed_cm_s > 50.0f) {
        return false;
    }
    BLE_ClearSafetyLock();
    BLE_ResetDrive();
    PID_Reset(&pidLeftSpeed);
    PID_Reset(&pidRightSpeed);
    g_speed_tune_target = target_speed_cm_s;
    g_speed_tune_active = true;
    g_ble_state = BLE_STATE_RUNNING;
    g_speed_tune_start_ms = mspm0_get_clock_ms();
    g_speed_tune_sum_l = g_speed_tune_sum_r = 0.0f;
    g_speed_tune_max_l = g_speed_tune_max_r = 0.0f;
    g_speed_tune_min_l = g_speed_tune_min_r = 10000.0f;
    g_speed_tune_samples = 0;
    return true;
}

static void SpeedTune_Stop(bool send_result)
{
    float avg_l, avg_r, peak, valley, ripple;
    g_speed_tune_active = false;
    /* 速度阶跃结束后才允许再次提交参数曲线。 */
    if (g_ble_state == BLE_STATE_RUNNING) g_ble_state = BLE_STATE_IDLE;
    BLE_ResetDrive();
    AppBLE_RemoteReset();
    targetSpeedLeft = 0; targetSpeedRight = 0;
    PID_SetTarget(&pidLeftSpeed, 0.0f); PID_SetTarget(&pidRightSpeed, 0.0f);

    if (!send_result || g_speed_tune_samples < 10U) return;
    avg_l = g_speed_tune_sum_l / (float)g_speed_tune_samples;
    avg_r = g_speed_tune_sum_r / (float)g_speed_tune_samples;
    peak = g_speed_tune_max_l > g_speed_tune_max_r ? g_speed_tune_max_l : g_speed_tune_max_r;
    valley = g_speed_tune_min_l < g_speed_tune_min_r ? g_speed_tune_min_l : g_speed_tune_min_r;
    ripple = peak - valley;
    /* App 负责给出下一组候选参数；MCU 只安全采样并回传质量指标。
     * 这样速度、循迹和航向三类参数可以用同一套 App 评分/回滚策略，不在板端盲改。 */
    {
        uint8_t resp[18];
        write_i16_le(resp, 0, (int16_t)(avg_l * 10.0f));
        write_i16_le(resp, 2, (int16_t)(avg_r * 10.0f));
        write_i16_le(resp, 4, (int16_t)(peak * 10.0f));
        write_i16_le(resp, 6, (int16_t)(valley * 10.0f));
        write_i16_le(resp, 8, (int16_t)(ripple * 10.0f));
        write_i16_le(resp, 10, (int16_t)(pidLeftSpeed.kp * 10.0f));
        write_i16_le(resp, 12, (int16_t)(pidLeftSpeed.ki * 1000.0f));
        write_i16_le(resp, 14, (int16_t)(pidLeftSpeed.kd * 10.0f));
        write_i16_le(resp, 16, (int16_t)(pidLeftSpeed.kf * 10.0f));
        BLE_Protocol_Send(BLE_CMD_SPEED_TUNE, resp, 18);
    }
}

/* ==================== 上位机循迹台架模式 ==================== */
/* 仅用于低速传感器/PID联调；独立于任何尚未确定的赛题任务。
 * g_line_debug_active / g_gray_cal_active 已前置到 AppSpeedTune_Start 上方。 */
static uint32_t g_line_lost_since_ms = 0;
/* App 触发的白底校准采用分步采样，避免校准期间屏蔽 S0 急停。 */
static uint16_t g_gray_cal_step = 0;
static uint16_t g_gray_cal_max[8] = {0};
/* H题离线白底采样：固定20次，App可选逐路均值或最小值；结果保留给BLE回传和CSV。 */
static bool     g_gray_capture_active = false;
static uint8_t  g_gray_capture_method = 0U;
static uint8_t  g_gray_capture_step = 0U;
static uint32_t g_gray_capture_sum[8] = {0};
static uint16_t g_gray_capture_min[8] = {0};
static uint16_t g_gray_capture_result[8] = {0};
#define GRAY_CAPTURE_SAMPLES 20U
#define LINE_DEBUG_SPEED       18.0f
#define LINE_DEBUG_LOST_MS     250U

/* `Digtal` 在传感器驱动中约定“白=1、黑=0”，Get_Gray_Digital() 再取反，
 * 所以返回位图里黑线为1、全白离线为0x00。仅供旧低速循迹调试使用；
 * 台架闭环另用当周期模拟量检测，以免滞回位图保留旧压线状态。 */
static uint8_t GrayHasBlackLine(uint8_t gray)
{
    return gray != 0x00U;
}

/* 最高优先级安全指令与独占任务切换的统一收尾。
 * 任何页面切换、急停、校准或整定开始前都先释放旧控制权，防止残留目标速度恢复。
 * 导出供 app_ble_ascii.c 在 STOP/BRK/EMG/IMU_CAL/GRAY_CAL/LINE_START/SPEED_TUNE 等命令时调用。 */
void AbortBleExclusiveWork(void)
{
    g_line_debug_active = false;
    g_line_lost_since_ms = 0;
    g_gray_cal_active = false;
    g_gray_capture_active = false;
    g_h1_capture_kind = 0U;
    g_h1_capture_method = 0U;
    led_off(&led_green);
    AppBench_Abort();
    Competition_Stop();
    AppIns_Stop();
    SpeedTune_Stop(false);
    ImuApp_CancelCalibrateGyro();
    BLE_ResetDrive();
    AppBLE_RemoteReset();
    targetSpeedLeft = 0.0f; targetSpeedRight = 0.0f;
    PID_SetTarget(&pidLeftSpeed, 0.0f);
    PID_SetTarget(&pidRightSpeed, 0.0f);
    PID_Reset(&pidLine);
    PID_Reset(&pidAngle);
    PID_Reset(&pidPosition);
}

/* ==================== 位置保持旁路标志 (isr.c 读取) ==================== */
bool g_poshold_active = false;

/* ==================== IMU 使能开关 ====================
 * g_imu_enabled 控制 10ms 控制回路中是否执行 ImuApp_Update()（常规姿态更新）。
 *
 * I2C0 总线上同时挂载了 MPU6050 (地址 0x68) 和 SSD1306 OLED (地址 0x3C)，
 * 两个设备地址不冲突，但 OLED 刷屏（AppOLED_ShowTime 每次约 50ms 占用）期间
 * 可能与 IMU 姿态更新碰撞，造成 I2C 总线 busy 或数据异常。
 *
 * 2026 电赛 H 题 (车载平衡滚球运动控制系统) 的比赛阶段只使用灰度循线，
 * 不依赖陀螺仪航向。因此在比赛运行期间可关闭 IMU 常规更新，让 I2C0
 * 带宽完全让给 OLED 显示及其他外设。
 *
 * 关闭策略:
 *   - g_imu_enabled = false 仅跳过 ImuApp_Update(&dt)，IMU 芯片仍保持上电状态。
 *   - IMU 校准流程 (ImuApp_IsCalibrating / ImuApp_CalibrateTask) 不受此开关影响，
 *     校准启动后仍会访问 I2C0，确保校准在任何模式下都能完成。
 *   - 比赛开始 (Competition_Start) 自动置 false，比赛结束 (Competition_Stop /
 *     Competition_SetFailed) 自动恢复为 true。
 *   - 调试模式、需要姿态数据或进行 IMU 校准前必须重新使能。
 *
 * 注意: 如果比赛任务段表中包含 YAW_HOLD / TURN_YAW / DRIVE_DISTANCE 等
 *       依赖 IMU 的原语，必须在启动前将 g_imu_enabled 设为 true。
 */
bool g_imu_enabled = true;

/* H题 App 专用实时循迹调试模式：OLED 暂停刷屏，避免与 MPU6050 争用 I2C0。
 * 此标志不启动电机、不改变循迹控制，仅决定显示与IMU的总线分配。 */
bool g_h1_app_debug_mode = false;

/* ==================== 灰度传感器 ==================== */
No_MCU_Sensor g_GraySensor;
/* 默认白/黑阈值仅供启动监测；台架循迹必须完成一次明确的白底采样后才允许启动。 */
/* H题确认后的白底常量启动即装载，比赛/调试不再强制每次重新标定。
 * 通过App“白底20次采样”得到的新数组后，再回填此处并重新烧录即可永久更新。 */
uint8_t g_gray_runtime_calibrated = 1U;
/* H题白底硬编码：2026-07-30 App静止采样20次的逐路最低值。
 * 上电直接加载，正常比赛无需重复白底校准；现场光照/安装改变时才重新采样并回填。 */
uint16_t white[8] = {2146, 1892, 2657, 2389, 2755, 1666, 1765, 1983};
uint16_t black[8] = {200,  200,  200,  200,  200,  200,  200,  200};

/* ==================== 灰度数字量 ==================== */
uint8_t Get_Gray_Digital(void)
{
    extern No_MCU_Sensor g_GraySensor;
    extern uint8_t Get_Digtal_For_User(No_MCU_Sensor *sensor);
    return ~Get_Digtal_For_User(&g_GraySensor);
}

/* 蓝牙与板载按键共用分步白底校准：每个 10ms 控制周期只采一组，S0 指令不会被 1 秒采样阻塞。
 * 导出供 app_ble_ascii.c 在 GRAY_CAL 命令时调用。 */
void GrayCalibrateStart(void)
{
    int i;
    g_gray_cal_active = true;
    g_gray_runtime_calibrated = 0U;
    g_gray_cal_step = 0;
    for (i = 0; i < 8; i++) g_gray_cal_max[i] = 0;
    led_on(&led_green);
    buzzer_on(&buzzer0); mspm0_delay_ms(40); buzzer_off(&buzzer0);
}

/* 返回 1 表示完成，0 表示仍在采样。 */
static uint8_t GrayCalibrateTask(void)
{
    uint16_t raw[8];
    int i;
    if (!g_gray_cal_active) return 1;
    No_Mcu_Ganv_Sensor_Task_Without_tick(&g_GraySensor);
    if (Get_Anolog_Value(&g_GraySensor, raw)) {
        for (i = 0; i < 8; i++) if (raw[i] > g_gray_cal_max[i]) g_gray_cal_max[i] = raw[i];
    }
    g_gray_cal_step++;
    if (g_gray_cal_step < 100U) return 0;
    for (i = 0; i < 8; i++) white[i] = g_gray_cal_max[i];
    No_MCU_Ganv_Sensor_Init(&g_GraySensor, white, black);
    g_gray_cal_active = false;
    g_gray_runtime_calibrated = 1U;
    led_off(&led_green);
    buzzer_on(&buzzer0); mspm0_delay_ms(40); buzzer_off(&buzzer0);
    return 1;
}

uint8_t GrayCaptureStart(uint8_t method)
{
    int i;
    if (g_gray_capture_active || g_gray_cal_active || method > 1U) return 0U;
    g_gray_capture_active = true;
    g_gray_capture_method = method;
    g_gray_capture_step = 0U;
    for (i = 0; i < 8; i++) {
        g_gray_capture_sum[i] = 0U;
        g_gray_capture_min[i] = 0xFFFFU;
        g_gray_capture_result[i] = 0U;
    }
    led_on(&led_green);
    App_Indicator_RequestBeepOnce();
    return 1U;
}

uint8_t GrayCaptureIsActive(void)
{
    return g_gray_capture_active ? 1U : 0U;
}

uint8_t GrayCaptureTask(void)
{
    uint16_t raw[8];
    int i;
    if (!g_gray_capture_active) return 3U;
    No_Mcu_Ganv_Sensor_Task_Without_tick(&g_GraySensor);
    if (!Get_Anolog_Value(&g_GraySensor, raw)) return 0U;
    for (i = 0; i < 8; i++) {
        g_gray_capture_sum[i] += raw[i];
        if (raw[i] < g_gray_capture_min[i]) g_gray_capture_min[i] = raw[i];
    }
    g_gray_capture_step++;
    if (g_gray_capture_step < GRAY_CAPTURE_SAMPLES) return 0U;
    for (i = 0; i < 8; i++) {
        g_gray_capture_result[i] = (g_gray_capture_method == 0U)
            ? (uint16_t)(g_gray_capture_sum[i] / GRAY_CAPTURE_SAMPLES)
            : g_gray_capture_min[i];
        /* 采样结果立即在RAM中生效；永久值仍由确认后回填white[]常量并重新烧录。 */
        white[i] = g_gray_capture_result[i];
    }
    No_MCU_Ganv_Sensor_Init(&g_GraySensor, white, black);
    g_gray_runtime_calibrated = 1U;
    g_gray_capture_active = false;
    led_off(&led_green);
    App_Indicator_RequestBeepOnce();
    return 1U;
}

void GrayCaptureGetResult(uint16_t out[8], uint8_t *method, uint8_t *samples)
{
    int i;
    if (out != 0) for (i = 0; i < 8; i++) out[i] = g_gray_capture_result[i];
    if (method != 0) *method = g_gray_capture_method;
    if (samples != 0) *samples = g_gray_capture_step;
}

/* H题App离线采样会话：kind=1陀螺仪60秒，kind=2白底20次。
 * 仅静止时可进入；完成事件在主循环中通过BLE_CMD_CAL_RESULT回传。 */
uint8_t H1CalibrationCaptureStart(uint8_t kind, uint8_t method)
{
    if (g_h1_capture_kind != 0U || Competition_GetState() == COMP_STATE_RUNNING ||
        g_line_debug_active || g_speed_tune_active || AppBench_IsActive()) return 0U;
    AbortBleExclusiveWork();
    if (kind == 1U) {
        g_imu_enabled = true;
        if (!ImuApp_StartCalibrateGyroSamples(6000U)) return 0U;
    } else if (kind == 2U) {
        if (!GrayCaptureStart(method)) return 0U;
    } else {
        return 0U;
    }
    g_h1_capture_kind = kind;
    g_h1_capture_method = method;
    g_ble_state = BLE_STATE_CALIB;
    return 1U;
}

static void H1CalibrationCaptureSendResult(uint8_t status)
{
    /* 统一20字节结果：IMU=[kind,status,method,gx,gy,gz,samples]；灰度=[kind,status,method,samples,white0..7]。 */
    uint8_t buf[20] = {0};
    int32_t gx, gy, gz;
    uint16_t valid_samples;
    uint16_t white_result[8];
    uint8_t method, samples;
    buf[0] = g_h1_capture_kind;
    buf[1] = status;
    buf[2] = g_h1_capture_method;
    if (g_h1_capture_kind == 1U) {
        ImuApp_GetLastCalibration(&gx, &gy, &gz, 0, 0, 0, &valid_samples);
        write_i32_le(buf, 3, gx);
        write_i32_le(buf, 7, gy);
        write_i32_le(buf, 11, gz);
        write_i16_le(buf, 15, (int16_t)valid_samples);
    } else if (g_h1_capture_kind == 2U) {
        GrayCaptureGetResult(white_result, &method, &samples);
        buf[2] = method;
        buf[3] = samples;
        for (uint8_t i = 0U; i < 8U; i++) write_i16_le(buf, (uint8_t)(4U + i * 2U), (int16_t)white_result[i]);
    }
    BLE_Protocol_Send(BLE_CMD_CAL_RESULT, buf, sizeof(buf));
    g_h1_capture_kind = 0U;
    g_h1_capture_method = 0U;
    g_ble_state = BLE_STATE_IDLE;
}

/* 调试 UART1 的 RX(PA9) 未接 USB-TTL 时保持空闲高，避免悬空输入接收噪声。
 * 放在用户源文件中，SysConfig 重新生成 ti_msp_dl_config.c 后仍会在初始化后覆盖为上拉配置。 */
static void ConfigureDebugUartRxPullup(void)
{
    DL_GPIO_initPeripheralInputFunctionFeatures(
        GPIO_UART_1_IOMUX_RX, GPIO_UART_1_IOMUX_RX_FUNC,
        DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
        DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
}

/* ==================== 主入口 ==================== */
int main(void)
{
    /* ---- 硬件外设初始化 ---- */
    SYSCFG_DL_init();
    ConfigureDebugUartRxPullup();
    printf("=== MSPM0G3507 SmartCar Template ===\r\n");
    SysTick_Init();

    led_init(&led_red);
    led_init(&led_green);
    led_init(&led_blue);
    buzzer_init(&buzzer0);

    /* 上电自检 */
    App_Indicator_PowerOnSelfTest();

    /* ---- IMU (MPU6050) ---- */
    App_Indicator_ImuResult(ImuApp_Init());

    /* ---- 按键 ---- */
    APP_Key_Init();

    /* ---- 蓝牙通信协议栈 (CH9141, UART3/PB12-PB13) ---- */
    BLE_Init();
    AppBLE_Init();
    BLE_DebugInit();
    AppIns_Init();
    Competition_Init();
    printf("BLE Init OK.\r\n");

    /* ---- K230 球位置通信 (UART2, PB15=TX/PB16=RX, 115200) ---- */
    BallVision_Init();
    BallPdMonitor_Init();
    BallTask3_Init();
#if STEPPER_TEST_MODE_ENABLE
    StepperTest_Init(); /* defaults to ENN released and position invalid. */
#endif
    NVIC_ClearPendingIRQ(UART_K230_INST_INT_IRQN);
    NVIC_EnableIRQ(UART_K230_INST_INT_IRQN);
    printf("Ball Vision (K230) Init OK.\r\n");

    /* ---- 应用模板 (空壳, 在此实现你的功能) ---- */
    AppTemplate_Init();
    led_on(&led_red);  /* 空闲红灯 */

    /* ---- 电机 ---- */
    Motor_ConfigInit(&motorLeft, PWM_0_INST, GPIO_PWM_0_C0_IDX,
                     PWM_0_INST, GPIO_PWM_0_C1_IDX, 0);
    Motor_ConfigInit(&motorRight, PWM_1_INST, GPIO_PWM_0_C0_IDX,
                     PWM_1_INST, GPIO_PWM_1_C1_IDX, 1);

    /* ---- 中断 ---- */
    NVIC_ClearPendingIRQ(TIMER_0_INST_INT_IRQN);
    NVIC_EnableIRQ(TIMER_0_INST_INT_IRQN);
    NVIC_ClearPendingIRQ(GPIO_ENCODER_INT_IRQN);
    NVIC_EnableIRQ(GPIO_ENCODER_INT_IRQN);
    Encoder_Driver_Init(&encoderLeft, 1);
    Encoder_Driver_Init(&encoderRight, 0);

    /* ---- PID ---- */
    PID_Init(&pidLeftSpeed, POSITION_TYPE, 60.0f, 0.0f, 0.5f,
             999.0f, -999.0f, 999.0f, -999.0f);
    PID_Init(&pidRightSpeed, POSITION_TYPE, 60.0f, 0.0f, 0.5f,
             999.0f, -999.0f, 999.0f, -999.0f);
    PID_SetFeedforward(&pidLeftSpeed, 8.0f);
    PID_SetFeedforward(&pidRightSpeed, 8.0f);

    PID_Init(&pidLine, POSITION_TYPE, 0.00105055f, 0.0f, 0.0000199f,
             999.0f, -999.0f, 120.0f, -120.0f);
    PID_SetTarget(&pidLine, 0.0f);

    PID_Init(&pidAngle, POSITION_TYPE, 0.165f, 0.0f, 0.01f,
             999.0f, -999.0f, 120.0f, -120.0f);
    PID_SetTarget(&pidAngle, 0.0f);

    /* 位置 PID 以“剩余距离脉冲”为输入，输出基准速度(cm/s)。
     * 节点 pos_kp 会在每次1m台架测试启动时覆写，初值仅作安全兜底。 */
    PID_Init(&pidPosition, POSITION_TYPE, 0.03f, 0.0f, 0.0f,
             999.0f, -999.0f, 60.0f, 0.0f);
    PID_SetTarget(&pidPosition, 0.0f);

    /* ---- ADC + 灰度 ---- */
    bsp_adc_init();
    No_MCU_Ganv_Sensor_Init(&g_GraySensor, white, black);
    NVIC_EnableIRQ(ADC12_0_INST_INT_IRQN);
    mspm0_delay_ms(50);

    targetSpeedLeft  = 0.0f;
    targetSpeedRight = 0.0f;
    PID_SetTarget(&pidLeftSpeed, 0.0f);
    PID_SetTarget(&pidRightSpeed, 0.0f);

    printf("Init OK.\r\n");

    /* ---- OLED 显示屏 (硬件 I2C0, 与 MPU6050 共用地址 0x3C) ---- */
    AppOLED_Init();
    AppOLED_ShowString(0, 0, "H-Ball Car", 16);
    AppOLED_ShowString(0, 2, "OLED Test OK", 8);
    printf("OLED Init OK.\r\n");

    /* ==================== 主循环 ==================== */
    uint32_t last_10ms = 0;

    while (1)
    {
        /* ---- 按键扫描 ---- */
        APP_Key_Task();

        /* ---- 指示器 ---- */
        App_IndicatorTask();

#if !STEPPER_TEST_MODE_ENABLE
        /* ---- Key1 长按2s: 灰度白校准 ---- */
        {
            static uint32_t key1_hold_start = 0;
            static bool     key1_armed = false;
            if (DL_GPIO_readPins(GPIO_KEY_PORT, GPIO_KEY_PIN_1_PIN) == 0)
            {
                if (!key1_armed) {
                    key1_armed = true;
                    key1_hold_start = mspm0_get_clock_ms();
                } else if (mspm0_get_clock_ms() - key1_hold_start > 2000) {
                    /* 板载按键与 App 走同一分步校准路径，不能再调用旧阻塞函数。 */
                    AbortBleExclusiveWork();
                    GrayCalibrateStart();
                    key1_armed = false;
                }
            } else {
                key1_armed = false;
            }
        }
#endif

#if STEPPER_TEST_MODE_ENABLE
        StepperTest_Task();
#endif

        /* ---- 应用主任务 (在 app_template.c 中实现) ---- */
        AppTemplate_Task();

        /* ================== 10ms 控制回路 ================== */
        if (mspm0_get_clock_ms() - last_10ms >= 10)
        {
            last_10ms = mspm0_get_clock_ms();

            /* 灰度采样 */
            No_Mcu_Ganv_Sensor_Task_Without_tick(&g_GraySensor);

            /* 从 RX 环形缓冲中取出数据并处理 */
            while (BLE_RxBufferCount() > 0) {
                uint8_t data = BLE_RxBufferDequeue();
                BLE_RxHandler(data);
            }

            /* 蓝牙协议栈: 收包分发 + 示教回放 + 遥测推送 */
            AppBLE_Task();
            /* ASCII 协议命令分发 (STOP/BRK/EMG/PING/IMU_CAL/MOTOR_STATUS/LINE_STATUS/
             * GRAY_CAL/LINE_START/SPEED_TUNE_START/PPx/PDx/BZx 等) — 已封装到 app_ble_ascii.c */
            AppBLE_AsciiHandle();

                /* 非阻塞独占任务：每 10ms 推进一小步，因此 S0 急停始终有机会抢占。 */
                /* IMU 常规姿态更新与 CAL 不能并发访问同一 I2C。
                 * 校准启动后只由 ImuApp_CalibrateTask 按 10ms 节拍读取一次，
                 * 避免同一个控制周期内重复读 MPU 而把 I2C 推入 Busy 状态。 */
                if (ImuApp_IsCalibrating()) {
                    /* 丢弃可能在 CAL 前已到达的 L/R 实时包，防止校准完成后旧速度复活。 */
                    BLE_ResetDrive();
                    uint8_t cal_result = ImuApp_CalibrateTask();
                    if (cal_result == 1U) {
                        if (g_h1_capture_kind == 1U) H1CalibrationCaptureSendResult(1U);
                        else {
                            g_ble_state = BLE_STATE_IDLE;
                            BLE_Protocol_SendAck(BLE_CMD_IMU_CAL);
                        }
                    } else if (cal_result == 2U) {
                        if (g_h1_capture_kind == 1U) H1CalibrationCaptureSendResult(2U);
                        else {
                            g_ble_state = BLE_STATE_ERROR;
                            BLE_Protocol_SendNack(BLE_CMD_IMU_CAL, BLE_ERR_BUSY);
                        }
                    }
                }
                else if (g_imu_enabled) {
                    float dt = 0.01f;
                    ImuApp_Update(&dt);
                }
                /* g_imu_enabled == false: 跳过 IMU 更新，节省 I2C0 带宽 */

                if (g_gray_cal_active) {
                    BLE_ResetDrive();
                    if (GrayCalibrateTask()) BLE_Protocol_SendAck(BLE_CMD_GRAY_CAL);
                }
                else if (g_gray_capture_active) {
                    BLE_ResetDrive();
                    uint8_t capture_result = GrayCaptureTask();
                    if (capture_result == 1U) H1CalibrationCaptureSendResult(1U);
                }
                /* 竞赛任务优先 (高于台架/整定/遥控/示教/INS)。
                 * T2 正常完成后仍由 Competition_Task 以 10ms 周期维持底盘位置保持，
                 * 直到 K2 停止、切换任务或重新启动。 */
                else if ((Competition_GetState() == COMP_STATE_RUNNING) ||
                         ((Competition_GetState() == COMP_STATE_FINISHED) &&
                          (Competition_GetSelectedMissionIndex() == 1U))) {
                    BLE_ResetDrive();
                    Competition_Task();
                }
                /* 速度 PID 自动整定优先：执行受限阶跃，完成后自动停车并回传新参数。 */
                else if (AppBench_IsActive()) {
                    /* 三种通用台架闭环在 app_template.c 内执行；它们只写速度目标，
                     * TIMA0 ISR 仍是唯一 PWM 写入者。 */
                    BLE_ResetDrive();
                    AppBench_Task();
                }
                else if (g_speed_tune_active) {
                    BLE_ResetDrive();
                    uint32_t tune_now = mspm0_get_clock_ms();
                    uint32_t elapsed = tune_now - g_speed_tune_start_ms;
                    if (elapsed >= SPEED_TUNE_TOTAL_MS) {
                        SpeedTune_Stop(true);
                    } else {
                        targetSpeedLeft = g_speed_tune_target;
                        targetSpeedRight = g_speed_tune_target;
                        if (elapsed >= SPEED_TUNE_SETTLE_MS) {
                            float tune_l = fabsf(Encoder_GetSpeedLine(&encoderLeft));
                            float tune_r = fabsf(Encoder_GetSpeedLine(&encoderRight));
                            g_speed_tune_sum_l += tune_l; g_speed_tune_sum_r += tune_r;
                            if (tune_l > g_speed_tune_max_l) g_speed_tune_max_l = tune_l;
                            if (tune_r > g_speed_tune_max_r) g_speed_tune_max_r = tune_r;
                            if (tune_l < g_speed_tune_min_l) g_speed_tune_min_l = tune_l;
                            if (tune_r < g_speed_tune_min_r) g_speed_tune_min_r = tune_r;
                            g_speed_tune_samples++;
                        }
                    }
                }
                /* 上位机低速循迹台架模式优先：丢线超过阈值自动停车，防止无人值守冲出赛道。 */
                else if (g_line_debug_active) {
                    BLE_ResetDrive();
                    uint8_t gray = Get_Gray_Digital();
                    uint32_t now_ms = mspm0_get_clock_ms();
                    if (!GrayHasBlackLine(gray)) {
                        if (g_line_lost_since_ms == 0) g_line_lost_since_ms = now_ms;
                        if (now_ms - g_line_lost_since_ms >= LINE_DEBUG_LOST_MS) {
                            g_line_debug_active = false;
                            AppBLE_RemoteReset();
                            targetSpeedLeft = 0; targetSpeedRight = 0;
                            PID_Reset(&pidLine);
                        }
                    } else {
                        float error = -CalculateNormalizedValue(&g_GraySensor, 0);
                        float steer = PID_Calc(&pidLine, error);
                        g_line_lost_since_ms = 0;
                        targetSpeedLeft = LINE_DEBUG_SPEED - steer;
                        targetSpeedRight = LINE_DEBUG_SPEED + steer;
                    }
                }
                /* 遥控/示教/指令解释器仲裁 — 已封装到 app_ble_remote.c
                 * 内部按 Teach → BLE 遥控 → AppIns 顺序接管速度。 */
                else {
                    AppBLE_RemoteDriveUpdate();
                }

            /* Visual PD executes at the fixed 10 ms application cadence. The limited
             * stepper follower accepts only the already-clamped position request; it
             * auto-zeros after its startup delay and starts only after fresh vision. */
#if STEPPER_TEST_MODE_ENABLE
            {
                BallPosition_t ball = BallVision_GetPosition();
                BallTask3Status_t task3;
                BallPdMonitor_t pd;
                BallMotionState_t motion;
                CompBallMode_e ball_mode = Competition_GetBallMode();
                CompetitionState_e competition_state = Competition_GetState();
                uint32_t age_ms = BallVision_TimeSinceLastFrame();
                /* 无任务时也必须维持钢球在O点；只有实际运行的带球任务才
                 * 接管为静止序列或运动前馈，任务选择本身不能关闭平衡。 */
                bool mission_running = (competition_state == COMP_STATE_RUNNING) ||
                                       (competition_state == COMP_STATE_FINISHED);
                bool static_ball_sequence = mission_running &&
                                            (ball_mode == COMP_BALL_MODE_STATIC_SEQUENCE);
                bool motion_balance = (competition_state == COMP_STATE_RUNNING) &&
                                      (ball_mode == COMP_BALL_MODE_MOTION_BALANCE);
                int16_t requested_steps;
                float speed_left = Encoder_GetSpeedLine(&encoderLeft);
                float speed_right = Encoder_GetSpeedLine(&encoderRight);
                float speed_avg = 0.5f * (speed_left + speed_right);
                float raw_accel = (speed_avg - s_ball_prev_vehicle_speed_cm_s) / 0.01f;

                s_ball_prev_vehicle_speed_cm_s = speed_avg;
                s_ball_vehicle_accel_cm_s2 += BALL_MOTION_ACCEL_FILTER_ALPHA *
                    (raw_accel - s_ball_vehicle_accel_cm_s2);

                /* 仅正在运行/已正常结束的静止任务执行 O -> +5cm -> -5cm。
                 * 空闲、停止、失败或其它底盘任务均回到O点平衡；运动滚球任务
                 * 只在运行时叠加车辆运动前馈。 */
                if (static_ball_sequence) {
                    task3 = BallTask3_Update(
                        &ball,
                        ball.valid && (age_ms <= BALL_PD_VISION_TIMEOUT_MS) &&
                        (ball.confidence >= BALL_PD_MIN_CONFIDENCE),
                        mspm0_get_clock_ms());
                    BallPdMonitor_SetTargetX(task3.target_x);
                } else {
                    BallPdMonitor_SetTargetX(BALL_PD_PHYSICAL_BALANCE_X_PX);
                    task3.target_x = BALL_PD_PHYSICAL_BALANCE_X_PX;
                }

                pd = BallPdMonitor_Update(&ball, age_ms);
                motion.speed_avg_cm_s = speed_avg;
                motion.speed_diff_cm_s = speed_right - speed_left;
                motion.accel_cm_s2 = s_ball_vehicle_accel_cm_s2;
                motion.yaw_rate_dps = g_imuData.yawRate;
                motion.imu_valid = g_imu_enabled && (g_imuData.hw_ok != 0U);
                requested_steps = BallPdMonitor_ApplyMotionCompensation(
                    pd.requested_steps, &motion, motion_balance);
                /* 静止滚球任务需要在O和最终-5cm都保持实测到位时的管道斜率。
                 * 运动平衡任务只在物理中心允许捕获保持，避免转弯/加速时锁死摆杆。 */
                StepperTest_UpdateVisualRequest(
                    requested_steps, pd.error_px, pd.vx, ball.seq,
                    (static_ball_sequence &&
                     ((task3.target_x == BALL_TASK3_CENTER_X_PX) ||
                      (task3.target_x == BALL_TASK3_MINUS_5CM_X_PX))) ||
                    ((task3.target_x == BALL_PD_PHYSICAL_BALANCE_X_PX) && !motion_balance),
                    pd.status == BALL_PD_OK);
            }
#endif

            /* 遥测帧推送 (在 10ms 循环里按设定频率发送) */
            AppBLE_TelemTrySend(mspm0_get_clock_ms());

            /* 速度目标 → 速度PID (ISR 读取) */
            PID_SetTarget(&pidLeftSpeed, targetSpeedLeft);
            PID_SetTarget(&pidRightSpeed, targetSpeedRight);


        }

        /* K230 UART2 接收状态：从主循环低频打印，便于UART1终端确认链路，
         * 不在RX中断内做格式化或发送，避免影响115200接收与控制实时性。 */
#if BALL_VISION_DEBUG_PRINT
        {
            static uint32_t last_ball_debug_ms = 0;
            uint32_t ball_debug_now = mspm0_get_clock_ms();
            if (ball_debug_now - last_ball_debug_ms >= BALL_VISION_DEBUG_PERIOD_MS) {
                BallPosition_t ball = BallVision_GetPosition();
                uint32_t age_ms = BallVision_TimeSinceLastFrame();
                uint32_t rx_errors = BallVision_GetRxErrorCount();
                last_ball_debug_ms = ball_debug_now;
                if (ball.valid) {
                    printf("K230 BALL: x=%d y=%d vx=%d vy=%d cf=%u seq=%lu age=%lums frames=%lu err=%lu\r\n",
                           (int)ball.x, (int)ball.y, (int)ball.vx, (int)ball.vy,
                           (unsigned int)ball.confidence, (unsigned long)ball.seq,
                           (unsigned long)age_ms, (unsigned long)ball.frame_cnt,
                           (unsigned long)rx_errors);
                } else {
                    printf("K230 BALL: waiting frames=0 err=%lu\r\n",
                           (unsigned long)rx_errors);
                }
            }
        }
#endif

        /* PD telemetry is deliberately decimated; the limited follower is updated
         * in the fixed 10 ms application cadence above. */
#if BALL_PD_MONITOR_PRINT
        {
            static uint32_t last_ball_pd_ms = 0U;
            uint32_t ball_pd_now = mspm0_get_clock_ms();
            if (ball_pd_now - last_ball_pd_ms >= BALL_PD_MONITOR_PERIOD_MS) {
                BallPosition_t ball = BallVision_GetPosition();
                uint32_t age_ms = BallVision_TimeSinceLastFrame();
                BallPdMonitor_t pd = BallPdMonitor_Update(&ball, age_ms);
                last_ball_pd_ms = ball_pd_now;
                printf("BALL_PD: st=%s fresh=%u x=%d ref=%d e=%d vx=%d vd=%d p=%.1f d=%.1f req=%d tgt=%d pos=%ld step_st=%s age=%lums cf=%u\r\n",
                       BallPdMonitor_StatusText(pd.status),
                       (unsigned int)(pd.status == BALL_PD_OK), (int)pd.x,
                       (int)pd.target_x, (int)pd.error_px, (int)pd.vx,
                       (int)pd.vx_for_d, (double)pd.p_term, (double)pd.d_term,
                       (int)pd.requested_steps,
#if STEPPER_TEST_MODE_ENABLE
                       (int)StepperTest_GetTargetSteps(),
                       (long)tmc2209_get_position_steps(),
                       StepperTest_StateText(StepperTest_GetState()),
#else
                       0, 0L, "DISABLED",
#endif
                       (unsigned long)pd.vision_age_ms,
                       (unsigned int)pd.confidence);
            }
        }
#endif

        /* OLED任务界面：任务/状态变化才整屏重绘；RUN 状态每500ms仅覆盖时间区域。
         * 避免原实现每500ms清写全部1024字节，显著减少I2C0阻塞。正式比赛期间
         * IMU关闭，OLED独占I2C0；App实时调试模式才暂停OLED刷新。 */
        {
            static uint32_t last_oled_ms = 0U;
            static uint8_t last_task = 0xFFU;
            static CompetitionState_e last_state = (CompetitionState_e)0xFF;
            uint32_t oled_now = mspm0_get_clock_ms();
            uint8_t task = Competition_GetSelectedMissionIndex() + 1U;
            CompetitionState_e comp_state = Competition_GetState();
            bool full_redraw = (task != last_task) || (comp_state != last_state);
            bool time_refresh = (comp_state == COMP_STATE_RUNNING) &&
                                (oled_now - last_oled_ms >= 500U);

            if (!g_h1_app_debug_mode && (full_redraw || time_refresh)) {
                last_oled_ms = oled_now;
                if (full_redraw) {
                    last_task = task;
                    last_state = comp_state;
                    AppOLED_Clear();
                    AppOLED_ShowString(0, 0, "TASK", 8);
                    AppOLED_ShowNum(30, 0, task, 1, 8);
                    AppOLED_ShowString(0, 1, Competition_GetMissionName(task - 1U), 8);
                    if (comp_state == COMP_STATE_RUNNING) {
                        AppOLED_ShowString(70, 0, "RUN", 8);
                        AppOLED_ShowTime(Competition_GetElapsedMs());
                    } else if (comp_state == COMP_STATE_FINISHED) {
                        AppOLED_ShowString(70, 0, "DONE", 8);
                        AppOLED_ShowTime(Competition_GetElapsedMs());
                    } else if (comp_state == COMP_STATE_FAILED) {
                        AppOLED_ShowString(70, 0, "FAIL", 8);
                        AppOLED_ShowTime(Competition_GetElapsedMs());
                    } else {
                        AppOLED_ShowString(70, 0, "READY", 8);
                        AppOLED_ShowTime(0U);
                        AppOLED_ShowString(0, 6, "K1< K2GO K3>", 8);
                    }
                } else {
                    AppOLED_ShowTime(Competition_GetElapsedMs());
                }
            }
        }
    }
}
