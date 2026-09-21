# 智能小车系统：MSPM0G3507 嵌入式固件 + Flutter BLE 上位机

> 2026 全国大学生电子设计竞赛（H 题·小车平衡球）参赛项目。本仓库包含完整的嵌入式固件源码、跨端上位机 App 源码与设计文档。

## 系统组成

```
┌──────────────┐   BLE (自研协议)   ┌─────────────────────┐
│  Flutter App │ ◄───────────────► │  MSPM0G3507 固件     │
│  (安卓/桌面)  │   遥控/参数/遥测   │  裸机 C · 四层架构    │
└──────────────┘                   └─────────┬───────────┘
                                             │ UART
                                   ┌─────────▼───────────┐
                                   │  K230 视觉模块       │
                                   │  YOLO 目标检测       │
                                   └─────────────────────┘
```

- **嵌入式固件**：约 1.5 万行 C，分层架构组织，覆盖外设驱动、中断、控制算法与竞赛任务逻辑
- **上位机 App**：约 1.2 万行 Dart，遥控 / 19 项参数读写 / 实时遥测 / PID 在线调参 / 路径示教五大模块，已交付安卓 APK

## 固件架构（firmware/）

```
UserCode/
├── UserApp/          应用层：竞赛任务、BLE 应用、示教、位置保持、OLED 页面
├── UserBsp/          板级支持：ADC、UART、中断服务(isr)、系统时基
├── UserDrivers/      设备驱动：BMI088 / MPU6050(IMU)、灰度传感器、编码器、
│                     TMC2209 步进电机、AT4950 电机、蜂鸣器、LED
└── UserMiddlewares/  中间件：PID 控制库、Mahony 姿态解算、BLE 协议/参数管理
```

技术要点：

- 裸机外设驱动：GPIO / 定时器 / PWM / ADC / UART，中断服务程序集中管理（`UserBsp/isr.c`）
- 传感器：BMI088 / MPU6050 六轴 IMU（I2C/SPI），多路灰度循迹传感器，正交编码器
- 执行器：TMC2209 步进电机驱动（UART 配置），AT4950 电机驱动
- 算法：增量/位置式 PID、Mahony AHRS 姿态解算、位置保持与路径示教
- 通讯：自研 BLE 应用层协议——XOR8 校验、心跳检测、断线重连、参数帧与遥测帧分离

## 上位机 App（app_lib/）

- 12 个页面：遥控、参数读写、遥测图表、PID 调参、路径示教、循迹调试、K230 视频回传（RTSP/H264 录制回放）等
- 自研 BLE 协议栈（`ble_protocol.dart` / `ble_handshake.dart`）：握手、XOR8 校验、心跳、断线重连
- 服务层与页面解耦（`services/`），参数模型与遥测帧模型化（`models/`）

## 目录结构

```
├── firmware/          MSPM0G3507 裸机固件（SysConfig 生成外设配置 + UserCode 分层代码）
├── app_lib/           Flutter 上位机源码（lib/）
├── android/ windows/  Flutter 平台工程（不含构建产物）
├── packages/          本地依赖的播放器插件
└── docs/              模块级设计文档与交接文档（23 篇）
```

## 构建说明

**固件**（Keil MDK + TI SysConfig）：

1. 用 TI SysConfig 打开 `firmware/empty.syscfg` 生成外设初始化代码（`ti_msp_dl_config.c/h`）
2. 将 `firmware/UserCode` 加入 Keil 工程编译，目标芯片 MSPM0G3507

**App**（Flutter 3.x）：

```bash
flutter pub get
flutter run            # 或 flutter build apk
```

## 文档索引（docs/）

固件模块文档 00–22：项目总览、AT4950 电机驱动、编码器、BMI088 IMU、灰度传感器、LED、蜂鸣器、中断服务、系统时钟、ADC、串口、PID 控制库、Mahony 姿态解算、IMU 应用、按键、指示器、底盘运动控制、主程序、调试、位置保持、模块索引、交接文档、完成度。
另有：H 题通讯方案（App 蓝牙 + K230 WiFi）、K230 视觉与串口联调交接、App 端完成度、UI 完成交接、UI 动画优化方案。

## 说明

- 本仓库为竞赛实践项目存档，代码为作者独立/主导开发
- 仅包含源码与文档，不含构建产物与数据日志
