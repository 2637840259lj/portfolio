# IMU 应用管理模块（App IMU）

## 概述

本模块是 IMU 子系统的上层管理器，参考项目 `8_i2c_mpu6050` 的 `imu_app.c/.h` 实现。负责：
- 初始化 MPU6050 传感器和 Mahony 姿态解算
- 以 **100Hz** 频率读取传感器数据并进行姿态融合
- 对外提供欧拉角（Roll/Pitch）和 Yaw 角积分

## 数据结构

```c
typedef struct {
    unsigned long lastTick;   // 上次更新时间戳 (ms)
    short accel[3];           // 加速度计原始 ADC
    short gyro[3];            // 陀螺仪原始 ADC (已校准)
    float ax, ay, az;         // 加速度 (g)
    float gx, gy, gz;         // 角速度 (rad/s)
    float roll, pitch;        // 绝对姿态（来自四元数）
    float yaw, yawRate;       // Yaw 角积分 + 角速度 (°/s)
    uint8_t hw_ok;            // IMU 硬件状态：0=失败, 1=正常
} ImuData_t;
```

## 核心函数

### ImuApp_Init()

**功能**：初始化 IMU 子系统。调用 `MPU_Init()` 和 `MPU_Calibrate_Gyro()`。

```c
uint8_t ImuApp_Init(void);
```

| 返回值 | 含义 |
|--------|------|
| 1 | 初始化成功 |
| 0 | 初始化失败（MPU6050 未连接或损坏） |

### ImuApp_Update(float *dt)

**功能**：读取传感器数据 → 物理量转换 → Mahony 姿态融合 → 输出欧拉角。

```c
uint8_t ImuApp_Update(float *dt);
```

**工作流程：**
1. 计算时间差 `*dt`
2. 设置 Mahony 采样频率
3. 读取 MPU6050 加速度计和陀螺仪数据
4. 将 ADC 原始值转为物理量（加速度 → g，角速度 → rad/s）
5. 调用 `MahonyAHRSupdateIMU()` 融合姿态
6. 从四元数计算 Roll / Pitch（绝对角度）
7. Yaw 通过角速度积分获得（相对角度，车用不依赖磁力计）

### ImuApp_CalibrateGyro()

**功能**：陀螺仪真实零偏校准（按键触发，阻塞约 1.2s）。

```c
uint8_t ImuApp_CalibrateGyro(void);
```

**触发方式**：空闲时 **Key2 长按 1s**（`app_key.c` KEEPALIVE 事件）。

**反馈流程**（无显示屏）：
1. 开始：哔 1 声 + 蓝灯常亮
2. 采样：`MPU_Calibrate_Gyro(200)` — 200 次 × 5ms ≈ 1s 阻塞采样
3. 成功：Yaw 清零 + 蓝灯灭 + 哔 2 声，新零偏打印到串口
4. 失败：蓝灯灭 + 哔 3 声

> ⚠️ 校准期间小车必须保持**绝对静止**，否则零偏不准会导致 Yaw 漂移。

## Yaw 角说明

由于 MPU6050 无磁力计，Yaw 角采用**角速度积分**方式：

```c
g_imuData.yaw += g_imuData.yawRate * dt;   // yawRate = gz * RAD2DEG
```

这意味着：
- Yaw 是**相对角度**（从系统启动时开始累计）
- 长时间运行会有**零点漂移**
- 适合短时间转弯检测，不适合长时间绝对航向

## 校准方式说明

| 校准方式 | 触发 | 说明 |
|----------|------|------|
| 硬编码零偏 | 开机 `ImuApp_Init()` 自动 | `MPU_Calibrate_Gyro(0)`，使用预设值 `{0,-9,-15}`，快速启动 |
| 真实采样零偏 | Key2 长按 1s（空闲） | `MPU_Calibrate_Gyro(200)`，实测 1s 平均，更准确 |

## 兼容性宏

```c
#define IMU_Init()       ImuApp_Init()
#define IMU_Task()       ImuApp_Update(&(float){0.01f})
#define IMU_GetAngles(r,p,y)  /* ... */
#define IMU_GetStatus()  (g_imuData.hw_ok ? 2 : 3)
```

这些宏保持与旧版 `app_imu.h` API 兼容，便于切换。
