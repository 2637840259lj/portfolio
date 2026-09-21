# 主程序（empty.c）

## 概述

项目唯一入口，负责硬件初始化 + 主循环调度。通过 `SYS_DEBUG_MODE` 宏切换调试/竞赛模式。

## 双模式切换

```c
// empty.c 第 22 行
#define SYS_DEBUG_MODE 1   // 1=调试模式, 0=竞赛模式
```

| 模式 | 任务调度 | 按键映射 | 用途 |
|------|----------|----------|------|
| 调试 (1) | `Debug_Task()` | 调试按键 | 编码器闭环验证 |
| 竞赛 (0) | `Competition_Task()` | 竞赛按键 | 4 任务状态机 |

## 初始化流程

```
SYSCFG_DL_init()          硬件初始化 (SysConfig 生成)
SysTick_Init()            1ms 时基
LED / 蜂鸣器 初始化
上电自检 (RGB 各闪一次)
ImuApp_Init()             MPU6050 I2C 初始化
APP_Key_Init()            按键 ebtn 初始化
Competition_Init() 或 Debug_Init()
Motor_ConfigInit() × 2    双电机 PWM
编码器中断 + 驱动初始化
PID × 4 初始化 (左速/右速/循迹/角度)
ADC + 灰度传感器初始化
```

## 主循环结构

```c
while (1) {
    APP_Key_Task();          // 按键扫描
    App_IndicatorTask();     // LED/蜂鸣器异步更新
    Key1 长按 2s 检测        // 灰度白校准 (空闲时)

    #if SYS_DEBUG_MODE
        Debug_Task();        // 调试测试
    #else
        Competition_Task();  // 竞赛任务
    #endif

    // ===== 10ms 控制回路 =====
    if (elapsed >= 10ms) {
        ImuApp_Update();     // 姿态更新 100Hz
        灰度传感器采样
        速度目标 → 速度PID   // ISR 执行 PID 计算
        串口调试输出 (1Hz)
    }
}
```

## TIMA0 中断（速度环）

```c
void TIMA0_IRQHandler(void)   // 10ms
{
    Encoder_Update(L);        // 编码器速度计算
    Encoder_Update(R);

    if (g_poshold_active) return;  // 位置保持模式: 跳过速度PID

    PID_Calc(pidLeftSpeed);   // 速度闭环
    PID_Calc(pidRightSpeed);
    Motor_SetSpeed(L/R);      // PWM 输出
}
```

## 速度 PID 参数（硬编码）

| PID | 类型 | Kp | Ki | Kd | 前馈 | 限幅 |
|-----|------|----|----|----|------|------|
| 左轮速度 | 位置式 | 60 | 0 | 0.5 | 8.0 | ±999 |
| 右轮速度 | 位置式 | 60 | 0 | 0.5 | 8.0 | ±999 |
| 循迹 | 位置式 | 0.00105 | 0 | 0.0000199 | — | ±120 |
| 角度 (保留) | 位置式 | 0.165 | 0 | 0.01 | — | ±120 |

## 全局变量

```c
float targetSpeedLeft;       // 左轮速度目标 (cm/s)，ISR 读取
float targetSpeedRight;      // 右轮速度目标 (cm/s)
Encoder_t encoderLeft/Right; // 编码器对象
PID_t    pidLeftSpeed/RightSpeed/Line/Angle;  // PID 对象
MOTOR_t  motorLeft/Right;    // 电机对象
```

## 灰度白校准

Key1 长按 2s（空闲时触发）：绿灯闪烁采样 1s，取 100 次最大值写入 `white[8]`。
白值/黑值硬编码在 `empty.c` 中，更换场地需重新标定。
