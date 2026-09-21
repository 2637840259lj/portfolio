# MPU6050 六轴 IMU 驱动模块

## 概述

本模块提供 InvenSense MPU6050 六轴惯性传感器（三轴加速度计 + 三轴陀螺仪）的 I2C 驱动，参考项目 `8_i2c_mpu6050` 移植。包含 SDA 总线解锁（防死锁）、寄存器读写、传感器初始化配置、原始数据读取、陀螺仪零偏校准功能。

## 硬件连接

MPU6050 与 OLED 共享 I2C0 总线（PA0=SDA, PA1=SCL, 100kHz）。

| 功能 | MCU 引脚 | I2C 地址 |
|------|----------|----------|
| SDA | PA0 (I2C0_SDA) | 0x68 (AD0=0) |
| SCL | PA1 (I2C0_SCL) | 或 0x69 (AD0=1) |

## 关键特性

### SDA 总线解锁 (mpu_i2c_sda_unlock)

I2C 从机可能因干扰拉低 SDA 导致总线死锁。本驱动在初始化时检测 SDA 状态，若为低电平则执行解锁流程：
1. 将 SCL 切换为 GPIO 输出，发送 100 个时钟脉冲
2. 每脉冲后检查 SDA 是否释放
3. 重新初始化 I2C 外设

### 初始化序列 (MPU_Init)

1. 复位 MPU6050（写 0x80 到 PWR_MGMT1）
2. 唤醒（写 0x00）
3. 配置陀螺仪量程 ±2000°/s
4. 配置加速度计量程 ±2g
5. 设置采样率 100Hz（最终值）
6. 读 WHO_AM_I (0x75)，验证 ID 为 0x68/0x70/0x71
7. 设置时钟源为 PLL（Gyro X）

### 校准 (MPU_Calibrate_Gyro)

使用硬编码零偏值 `s_gyroBias[3] = {0, -9, -15}`（原始 ADC 值），由参考项目提供。

## API 列表

| 函数 | 说明 |
|------|------|
| `MPU_Init()` | 初始化传感器（复位→配置→验证 ID） |
| `MPU_Set_Gyro_Fsr(fsr)` | 设置陀螺仪量程 (0=±250, 1=±500, 2=±1000, 3=±2000 °/s) |
| `MPU_Set_Accel_Fsr(fsr)` | 设置加速度计量程 (0=±2g, 1=±4g, 2=±8g, 3=±16g) |
| `MPU_Set_Rate(rate)` | 设置输出速率 (4~1000Hz)，自动配置低通滤波器 |
| `MPU_Get_Gyroscope(gx, gy, gz)` | 读取陀螺仪原始值 (ADC) |
| `MPU_Get_Gyroscope_Calibrated(gx, gy, gz)` | 读取已校准陀螺仪 |
| `MPU_Get_Accelerometer(ax, ay, az)` | 读取加速度计原始值 (ADC) |
| `MPU_Get_Temperature()` | 读取温度 (×100) |
| `MPU_Calibrate_Gyro(samples)` | 陀螺仪零偏校准（当前使用硬编码值） |
| `MPU_Write_Byte/Read_Byte` | 单字节寄存器读写 |
| `MPU_Write_Len/Read_Len` | 多字节寄存器读写 |

## 数据流

```
MPU6050 传感器
    ↓ I2C 读取 (100Hz)
MPU_Get_Accelerometer + MPU_Get_Gyroscope_Calibrated
    ↓ 物理量转换
app_imu.c (ImuApp_Update)
    ↓ MahonyAHRSupdateIMU
Roll / Pitch / Yaw
    ↓
OLED 显示 / (预留角度控制)
```
