#ifndef __APP_IMU_H__
#define __APP_IMU_H__

#include <stdint.h>

typedef struct
{
    unsigned long lastTick;
    short accel[3];
    short gyro[3];
    float ax, ay, az;
    float gx, gy, gz;
    float roll, pitch, yaw;
    float yawRate;
    uint8_t hw_ok;   /* 0=IMU初始化失败, 1=成功 */
} ImuData_t;

extern ImuData_t g_imuData;

/**
 * @brief 初始化 MPU6050 + Mahony（硬编码零偏，快速启动）
 * @return 1=成功, 0=IMU 通讯失败
 */
uint8_t ImuApp_Init(void);

/**
 * @brief IMU 周期任务（建议 100Hz 调用）
 * @param dt 输入建议周期(秒)，函数内部会按实际间隔修正
 * @return 1=本帧有更新, 0=无更新/硬件不可用
 */
uint8_t ImuApp_Update(float *dt);

/**
 * @brief 陀螺仪真实零偏校准（阻塞约 2 秒，需保持绝对静止）
 * @details 蓝灯常亮+哔1声开始 → 阻塞采样 → 成功哔2声 / 失败哔3声
 * @return 1=成功, 0=失败
 */
uint8_t ImuApp_CalibrateGyro(void);

/**
 * @brief 启动非阻塞陀螺仪零偏校准。主循环须持续调用 ImuApp_CalibrateTask。
 * @return 1=已启动, 0=硬件不可用或已在校准
 */
uint8_t ImuApp_StartCalibrateGyro(void);
/** 启动可指定时长的非阻塞陀螺仪原始零偏采样；sample_count按10ms计。 */
uint8_t ImuApp_StartCalibrateGyroSamples(uint16_t sample_count);

/**
 * @brief 推进一次非阻塞校准采样。
 * @return 0=进行中, 1=成功, 2=失败, 3=已取消/空闲
 */
uint8_t ImuApp_CalibrateTask(void);
void    ImuApp_CancelCalibrateGyro(void);
uint8_t ImuApp_IsCalibrating(void);

/** 当前上位机触发的静止零偏校准是否已成功完成（上电默认无效）。 */
uint8_t ImuApp_HasRuntimeCalibration(void);

/**
 * @brief 将相对航向角设为 0°（不改变已标定的陀螺仪零偏）。
 */
void ImuApp_ResetYaw(void);

/* 上位机静止闭环校准：仅允许小步调整 Z 轴原始零偏，单位为 MPU 原始 LSB。 */
void    ImuApp_AdjustGyroBiasZ(int16_t delta_lsb);
int32_t ImuApp_GetGyroBiasZ(void);
/** 最近一次非阻塞校准的原始均值和有效采样数；仅在任务成功后有效。 */
void ImuApp_GetLastCalibration(int32_t *gx, int32_t *gy, int32_t *gz,
                                int32_t *ax, int32_t *ay, int32_t *az,
                                uint16_t *valid_samples);

/* 兼容旧版 app_imu API */
#define IMU_Init()       ImuApp_Init()
#define IMU_Task()       ImuApp_Update(&(float){0.01f})
#define IMU_GetAngles(r,p,y)  do{ if(r)*(r)=g_imuData.roll; if(p)*(p)=g_imuData.pitch; if(y)*(y)=g_imuData.yaw; }while(0)
#define IMU_GetStatus()  (g_imuData.hw_ok ? 2 : 3)

#endif
