# Mahony AHRS 姿态解算算法

## 概述

Mahony AHRS（Attitude and Heading Reference System）是基于四元数的互补滤波姿态解算算法，由 SOH Madgwick 提出。本模块用于融合 MPU6050 陀螺仪和加速度计数据，计算小车的实时姿态欧拉角（Roll/Pitch）。参考项目 `8_i2c_mpu6050` 实现。

> **说明**：小车转弯使用编码器差速方案，不依赖 IMU。IMU 的姿态数据仅用于 OLED 显示辅助调试。

## 算法参数

```c
#define twoKpDef  (2.0f * 0.5f)   // 2 × 比例增益 Kp = 1.0
#define twoKiDef  (2.0f * 0.0f)   // 2 × 积分增益 Ki = 0（不使用积分项）
```

| 参数 | 值 | 说明 |
|------|-----|------|
| Kp | 0.5 | 比例增益，决定加速度计对姿态修正的强度 |
| Ki | 0.0 | 积分增益（关闭，防止积分漂移） |
| 采样频率 | 动态 | 由 ImuApp_Update 传入 dt 计算 |

## 全局变量

| 变量 | 说明 |
|------|------|
| `q0, q1, q2, q3` | 四元数（传感器系 → 导航系），`extern volatile` 可被 app_imu.c 直接访问 |
| `integralFBx/y/z` | 积分反馈误差累积 |
| `sampleFreq` | 当前采样频率 |
| `twoKp / twoKi` | 当前增益值 |

## API 函数

### 1. MahonyAHRSupdateIMU()

**核心函数**：仅使用陀螺仪 + 加速度计进行姿态融合（无磁力计）。

```c
void MahonyAHRSupdateIMU(float gx, float gy, float gz, float ax, float ay, float az);
```

| 参数 | 单位 | 说明 |
|------|------|------|
| gx, gy, gz | rad/s | 陀螺仪角速度（已归一化） |
| ax, ay, az | g | 加速度计（已归一化） |

**流程：**
1. 加速度计归一化
2. 估计重力方向（来自当前四元数）
3. 计算加速度计估计值与测量值的叉积误差
4. 比例-积分反馈修正角速度
5. 四元数微分方程更新
6. 四元数归一化

### 2. MahonyAHRSupdate()

**完整版**：同时使用磁力计 + 陀螺仪 + 加速度计。此项目中未使用（MPU6050 无磁力计）。

```c
void MahonyAHRSupdate(float gx, float gy, float gz, float ax, float ay, float az,
                      float mx, float my, float mz);
```

### 3. MahonyAHRSSetSampleFreq()

设置采样频率，更新内部 `sampleFreq` 变量。

```c
void MahonyAHRSSetSampleFreq(float freq);
```

## Roll / Pitch 计算

在 `app_imu.c` 的 `ImuApp_Update()` 中，从四元数直接计算欧拉角：

```c
// Roll: 绕 X 轴
g_imuData.roll  = atan2f(2.0f * (q0 * q1 + q2 * q3),
                         1.0f - 2.0f * (q1 * q1 + q2 * q2)) * RAD2DEG;

// Pitch: 绕 Y 轴
g_imuData.pitch = asinf(2.0f * (q0 * q2 - q3 * q1)) * RAD2DEG;

// Yaw: 用角速度积分（非磁力计）
g_imuData.yawRate = g_imuData.gz * RAD2DEG;
g_imuData.yaw += g_imuData.yawRate * dt;
```
