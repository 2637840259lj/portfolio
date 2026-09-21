/**
 * @file      app_imu.c
 * @brief     IMU 数据采集与姿态解算（基于 8_i2c_mpu6050 参考项目）
 * @details   独立管理 MPU6050 传感器读取、Mahony 姿态融合算法
 *           - ImuApp_Init(): 初始化 MPU6050 + Mahony，并加载已确认的硬编码零偏
 *           - ImuApp_Update(): 读取传感器 → 姿态解算 → 输出欧拉角 + Yaw 积分
 *           - ImuApp_CalibrateGyro(): 真实零偏校准（阻塞约 2 秒，需保持静止）
 * @note      若 MPU6050 硬件通讯失败，hw_ok = 0，所有函数静默返回
 */

#include "app_imu.h"
#include "MahonyAHRS.h"
#include "mpu6050.h"
#include "sys_time.h"
#include "led.h"
#include "buzzer.h"
#include "app_indicator.h"
#include <math.h>
#include <stdio.h>

#define ACCEL_SCALE 16384.0f
#define GYRO_SCALE  16.4f
#define DEG2RAD     0.01745329252f
#define RAD2DEG     57.2957795f

/* 静止自适应零偏：仅在车体稳定约 1 秒后缓慢补偿 MPU6050 热漂。 */
#define IMU_STILL_GYRO_DPS  0.8f
#define IMU_STILL_ACCEL_TOL 0.12f
#define IMU_STILL_SAMPLES   100U
#define IMU_AUTO_BIAS_ALPHA 0.01f

ImuData_t g_imuData = {0};
static float    s_auto_bias_z_dps = 0.0f;
static uint16_t s_still_samples = 0;

/* 非阻塞蓝牙校准状态：每 10ms 最多采样一次，主循环仍可接收 EMG/BRK。 */
#define IMU_CAL_SAMPLES       300U
#define IMU_CAL_MAX_SAMPLES   6000U
#define IMU_CAL_INTERVAL_MS   10U
#define IMU_CAL_MIN_VALID     270U
static uint8_t  s_cal_active = 0;
/* 上电自检的阻塞校准只保障传感器启动；台架航向/距离必须完成一次 App 明确请求的静止校准。 */
static uint8_t  s_runtime_calibrated = 0;
static uint16_t s_cal_taken = 0, s_cal_valid = 0, s_cal_fail_streak = 0;
static uint16_t s_cal_target_samples = IMU_CAL_SAMPLES;
static uint32_t s_cal_last_ms = 0;
#define IMU_CAL_MAX_CONSECUTIVE_I2C_FAIL 5U
static int32_t  s_cal_gx = 0, s_cal_gy = 0, s_cal_gz = 0;
static int32_t  s_cal_ax = 0, s_cal_ay = 0, s_cal_az = 0;
static int32_t  s_last_cal_gx = 0, s_last_cal_gy = 0, s_last_cal_gz = 0;
static int32_t  s_last_cal_ax = 0, s_last_cal_ay = 0, s_last_cal_az = 0;
static uint16_t s_last_cal_valid = 0;

/* ==================== 初始化 ==================== */

uint8_t ImuApp_Init(void)
{
    if (MPU_Init() != 0)
    {
        g_imuData.hw_ok = 0;
        return 0;
    }
    /* 启动时直接装载已确认的硬编码零偏，不能每次上电重新采样并覆盖它。
     * 需要重新估计时，由App显式启动60秒静止采样；确认结果后再回填驱动常量并烧录。 */
    {
        extern int32_t s_gyroBias[3];
        extern int32_t s_accelBias[3];
        MPU_SetCalibrationBias(s_gyroBias[0], s_gyroBias[1], s_gyroBias[2],
                               s_accelBias[0], s_accelBias[1], s_accelBias[2] + 16384);
    }
    g_imuData.hw_ok = 1;
    s_auto_bias_z_dps = 0.0f;
    s_still_samples = 0;
    s_runtime_calibrated = 0;
    ImuApp_ResetYaw();
    return 1;
}

/* ==================== 陀螺仪真实零偏校准 ==================== */

/**
 * @brief 真实零偏校准（阻塞约 2 秒）
 * @details 蓝灯常亮+哔1声开始 → 阻塞采样约 2 秒 → 蓝灯灭+哔2声完成
 *          校准期间小车必须保持绝对静止！
 *          校准后 Yaw 清零，零偏立即生效。
 * @return 1=成功, 0=失败(IMU硬件不可用或采样失败)
 */
uint8_t ImuApp_CalibrateGyro(void)
{
    extern buzzer_device_t buzzer0;
    extern int32_t s_gyroBias[3];   /* mpu6050.c 中的零偏数组 */

    if (!g_imuData.hw_ok)
        return 0;

    /* 开始: 哔 1 声 + 蓝灯常亮 (校准期间保持静止!) */
    buzzer_on(&buzzer0); mspm0_delay_ms(100); buzzer_off(&buzzer0);
    led_on(&led_blue);
    mspm0_delay_ms(200);

    /* 阻塞采样 400 次 × 5ms = 2 秒（保持静止）。 */
    uint8_t ret = MPU_Calibrate_Gyro(400);

    if (ret != 0)
    {
        /* 失败: 蓝灯灭 + 哔 3 声 */
        led_off(&led_blue);
        for (int k = 0; k < 3; k++) {
            buzzer_on(&buzzer0); mspm0_delay_ms(80); buzzer_off(&buzzer0);
            mspm0_delay_ms(80);
        }
        return 0;
    }

    /* 成功: 清零动态补偿与 Yaw，蓝灯灭 + 哔 2 声。 */
    s_auto_bias_z_dps = 0.0f;
    s_still_samples = 0;
    ImuApp_ResetYaw();
    led_off(&led_blue);
    buzzer_on(&buzzer0); mspm0_delay_ms(60); buzzer_off(&buzzer0);
    mspm0_delay_ms(60);
    buzzer_on(&buzzer0); mspm0_delay_ms(60); buzzer_off(&buzzer0);

    /* MPU_Calibrate_Gyro 内部已打印 6 个零偏值 */
    return 1;
}

uint8_t ImuApp_StartCalibrateGyroSamples(uint16_t sample_count)
{
    if (!g_imuData.hw_ok || s_cal_active || sample_count == 0U || sample_count > IMU_CAL_MAX_SAMPLES) return 0;
    s_cal_active = 1;
    s_cal_target_samples = sample_count;
    s_cal_taken = s_cal_valid = s_cal_fail_streak = 0;
    s_cal_gx = s_cal_gy = s_cal_gz = 0;
    s_cal_ax = s_cal_ay = s_cal_az = 0;
    s_cal_last_ms = 0;
    led_on(&led_blue);
    /* 以指示器异步鸣叫，既保留“收到 CAL”的可见反馈，也不阻塞主循环。 */
    App_Indicator_RequestBeepOnce();
    return 1;
}

uint8_t ImuApp_StartCalibrateGyro(void)
{
    return ImuApp_StartCalibrateGyroSamples(IMU_CAL_SAMPLES);
}

uint8_t ImuApp_IsCalibrating(void)
{
    return s_cal_active;
}

uint8_t ImuApp_HasRuntimeCalibration(void)
{
    return s_runtime_calibrated;
}

void ImuApp_CancelCalibrateGyro(void)
{
    if (!s_cal_active) return;
    s_cal_active = 0;
    led_off(&led_blue);
}

uint8_t ImuApp_CalibrateTask(void)
{
    short gx, gy, gz, ax, ay, az;
    uint32_t now;
    extern buzzer_device_t buzzer0;
    if (!s_cal_active) return 3;
    now = mspm0_get_clock_ms();
    if (s_cal_last_ms != 0 && now - s_cal_last_ms < IMU_CAL_INTERVAL_MS) return 0;
    s_cal_last_ms = now;

    if (MPU_Get_Gyroscope(&gx, &gy, &gz) == 0 &&
        MPU_Get_Accelerometer(&ax, &ay, &az) == 0) {
        s_cal_gx += gx; s_cal_gy += gy; s_cal_gz += gz;
        s_cal_ax += ax; s_cal_ay += ay; s_cal_az += az;
        s_cal_valid++;
        s_cal_fail_streak = 0;
    } else {
        /* I2C 已在驱动层尝试恢复；连续失败时尽早结束校准，
         * 主循环可立即恢复处理蓝牙而不是继续占用约3秒采样窗口。 */
        s_cal_fail_streak++;
        if (s_cal_fail_streak >= IMU_CAL_MAX_CONSECUTIVE_I2C_FAIL) {
            s_cal_active = 0;
            led_off(&led_blue);
            return 2;
        }
    }
    s_cal_taken++;
    if (s_cal_taken < s_cal_target_samples) return 0;

    s_cal_active = 0;
    led_off(&led_blue);
    if (s_cal_valid < IMU_CAL_MIN_VALID) {
        buzzer_on(&buzzer0); mspm0_delay_ms(80); buzzer_off(&buzzer0);
        return 2;
    }
    s_last_cal_gx = s_cal_gx / (int32_t)s_cal_valid;
    s_last_cal_gy = s_cal_gy / (int32_t)s_cal_valid;
    s_last_cal_gz = s_cal_gz / (int32_t)s_cal_valid;
    s_last_cal_ax = s_cal_ax / (int32_t)s_cal_valid;
    s_last_cal_ay = s_cal_ay / (int32_t)s_cal_valid;
    s_last_cal_az = s_cal_az / (int32_t)s_cal_valid;
    s_last_cal_valid = s_cal_valid;
    MPU_SetCalibrationBias(s_last_cal_gx, s_last_cal_gy, s_last_cal_gz,
                           s_last_cal_ax, s_last_cal_ay, s_last_cal_az);
    s_auto_bias_z_dps = 0.0f;
    s_still_samples = 0;
    s_runtime_calibrated = 1;
    ImuApp_ResetYaw();
    buzzer_on(&buzzer0); mspm0_delay_ms(50); buzzer_off(&buzzer0);
    return 1;
}

void ImuApp_ResetYaw(void)
{
    g_imuData.yaw = 0.0f;
    g_imuData.yawRate = 0.0f;
}

void ImuApp_AdjustGyroBiasZ(int16_t delta_lsb)
{
    MPU_AdjustGyroBiasZ(delta_lsb);
    /* 原始零偏更新后，清除临时热漂估计，避免补偿重复叠加。 */
    s_auto_bias_z_dps = 0.0f;
    s_still_samples = 0;
}

int32_t ImuApp_GetGyroBiasZ(void)
{
    return MPU_GetGyroBiasZ();
}

void ImuApp_GetLastCalibration(int32_t *gx, int32_t *gy, int32_t *gz,
                               int32_t *ax, int32_t *ay, int32_t *az,
                               uint16_t *valid_samples)
{
    if (gx != 0) *gx = s_last_cal_gx;
    if (gy != 0) *gy = s_last_cal_gy;
    if (gz != 0) *gz = s_last_cal_gz;
    if (ax != 0) *ax = s_last_cal_ax;
    if (ay != 0) *ay = s_last_cal_ay;
    if (az != 0) *az = s_last_cal_az;
    if (valid_samples != 0) *valid_samples = s_last_cal_valid;
}

/* ==================== 姿态更新 ==================== */

uint8_t ImuApp_Update(float *dt)
{
    unsigned long now = 0;

    if (!g_imuData.hw_ok)
        return 0;

    now = mspm0_get_clock_ms();

    if (now == g_imuData.lastTick)
        return 0;

    if (g_imuData.lastTick != 0)
    {
        *dt = (float)(now - g_imuData.lastTick) * 0.001f;
        if (*dt < 0.001f || *dt > 0.02f)
            *dt = 0.01f;
    }
    g_imuData.lastTick = now;
    MahonyAHRSSetSampleFreq(1.0f / *dt);

    MPU_Get_Accelerometer_Calibrated(&g_imuData.accel[0], &g_imuData.accel[1], &g_imuData.accel[2]);
    MPU_Get_Gyroscope_Calibrated(&g_imuData.gyro[0], &g_imuData.gyro[1], &g_imuData.gyro[2]);

    g_imuData.ax = (float)g_imuData.accel[0] / ACCEL_SCALE;
    g_imuData.ay = (float)g_imuData.accel[1] / ACCEL_SCALE;
    g_imuData.az = (float)g_imuData.accel[2] / ACCEL_SCALE;
    g_imuData.gx = ((float)g_imuData.gyro[0] / GYRO_SCALE) * DEG2RAD;
    g_imuData.gy = ((float)g_imuData.gyro[1] / GYRO_SCALE) * DEG2RAD;
    g_imuData.gz = ((float)g_imuData.gyro[2] / GYRO_SCALE) * DEG2RAD;

    MahonyAHRSupdateIMU(g_imuData.gx, g_imuData.gy, g_imuData.gz,
                        g_imuData.ax, g_imuData.ay, g_imuData.az);

    /* 从四元数计算 roll/pitch（绝对，不依赖磁力计） */
    g_imuData.roll  = atan2f(2.0f * (q0 * q1 + q2 * q3),
                             1.0f - 2.0f * (q1 * q1 + q2 * q2)) * RAD2DEG;
    g_imuData.pitch = asinf(2.0f * (q0 * q2 - q3 * q1)) * RAD2DEG;

    /* Yaw 用角速度积分（相对角度，车用不依赖磁力计）。
     * 仅在静止时慢速估计温漂余量；运动中完全冻结补偿，避免吞掉真实转向。 */
    float gz_dps = g_imuData.gz * RAD2DEG;
    float accel_norm = sqrtf(g_imuData.ax * g_imuData.ax +
                             g_imuData.ay * g_imuData.ay +
                             g_imuData.az * g_imuData.az);
    if (fabsf(gz_dps) < IMU_STILL_GYRO_DPS &&
        fabsf(accel_norm - 1.0f) < IMU_STILL_ACCEL_TOL) {
        if (s_still_samples < IMU_STILL_SAMPLES) s_still_samples++;
        if (s_still_samples >= IMU_STILL_SAMPLES) {
            s_auto_bias_z_dps += IMU_AUTO_BIAS_ALPHA *
                                 (gz_dps - s_auto_bias_z_dps);
        }
    } else {
        s_still_samples = 0;
    }
    g_imuData.yawRate = gz_dps - s_auto_bias_z_dps;
    g_imuData.yaw += g_imuData.yawRate * (*dt);

    return 1;
}
