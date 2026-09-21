# PID 控制库

## 概述

通用 PID 控制算法库，支持 **位置式 PID** 和 **增量式 PID** 两种经典算法，提供积分限幅、输出限幅和前馈补偿。本项目使用该库实现了 4 个 PID 控制器。

## 数据结构

### PID_Type_t（PID 类型枚举）

```c
typedef enum {
    POSITION_TYPE,  // 位置式 PID：u = Kp·e + Ki·∫e + Kd·de/dt + Kf·target
    DELTA_TYPE      // 增量式 PID：Δu = Kp·Δe + Ki·e + Kd·(e-2·e₋₁+e₋₂)
} PID_Type_t;
```

### PID_t（PID 结构体）

```c
typedef struct {
    PID_Type_t pid_type;          // PID 类型
    float kp;                      // 比例系数 Kp
    float ki;                      // 积分系数 Ki
    float kd;                      // 微分系数 Kd
    float kf;                      // 前馈系数 Kf

    float sum_error_limit_p;       // 积分限幅上限
    float sum_error_limit_n;       // 积分限幅下限
    float output_limit_p;          // 输出限幅上限
    float output_limit_n;          // 输出限幅下限

    float target;                  // 目标值
    float error;                   // 上一次偏差值 e(t-1)
    float pre_error;               // 上上次偏差值 e(t-2)，仅增量式使用
    float sum_error;               // 累计积分值，仅位置式使用
    float output;                  // PID 输出值
} PID_t;
```

## 本项目 PID 实例配置汇总

| PID 对象 | 类型 | Kp | Ki | Kd | Kf | 输出限幅 | 用途 |
|----------|------|----|----|----|----|----------|------|
| `pidLeftSpeed` | POSITION | 60.0 | 0.0 | 0.5 | 8.0 | ±999 | 左轮速度环 |
| `pidRightSpeed` | POSITION | 60.0 | 0.0 | 0.5 | 8.0 | ±999 | 右轮速度环 |
| `pidLine` | POSITION | 0.00105 | 0.0 | 0.00002 | 0.0 | ±120 | 循迹位置环 |
| `pidAngle` | POSITION | 0.165 | 0.0 | 0.01 | 0.0 | ±120 | 角度闭环 |

> 所有 PID 均使用位置式 + PD 控制（Ki=0），速度环额外带有前馈补偿。

## API 函数详解

### 1. PID_Init()

**功能**：完整初始化 PID 控制器（含限幅参数）。

```c
void PID_Init(
    PID_t *pid,                      // PID 对象指针
    const PID_Type_t pid_type,       // 类型：POSITION_TYPE 或 DELTA_TYPE
    const float kp,                   // 比例系数
    const float ki,                   // 积分系数
    const float kd,                   // 微分系数
    const float sum_error_limit_p,    // 积分上限
    const float sum_error_limit_n,    // 积分下限
    const float output_limit_p,       // 输出上限
    const float output_limit_n        // 输出下限
);
```

### 2. PID_InitSimple()

**功能**：简化版初始化，不限幅。

```c
void PID_InitSimple(
    PID_t *pid,
    const PID_Type_t pid_type,
    const float kp, const float ki, const float kd
);
```

### 3. PID_SetTarget()

**功能**：更新 PID 目标值。

```c
void PID_SetTarget(PID_t *pid, const float target);
```

| 参数 | 说明 |
|------|------|
| `pid` | PID 对象指针 |
| `target` | 新的目标值 |

### 4. PID_SetFeedforward()

**功能**：设置前馈系数 Kf。前馈项 = Kf × target，独立于误差，可加快响应速度。

```c
void PID_SetFeedforward(PID_t *pid, const float kf);
```

| 参数 | 说明 |
|------|------|
| `pid` | PID 对象指针 |
| `kf` | 前馈系数 |

**前馈在速度环中的应用**：
- PWM 满量程 999 / 最大速度约 120cm/s ≈ 8.3
- 设定 Kf = 8.0，使得目标速度 50cm/s 时产生约 400 的基准 PWM 输出
- PD 在此基础上微调，提高速度响应


### 5. PID_SetSumError()

**功能**：手动设置积分累计值（用于高自由度积分控制）。

```c
void PID_SetSumError(PID_t *pid, const float sum_error);
```

| 参数 | 说明 |
|------|------|
| `pid` | PID 对象指针 |
| `sum_error` | 新的积分累计值 |

### 6. PID_Calc() ★核心函数

**功能**：执行一次 PID 计算，返回控制输出。

```c
float PID_Calc(PID_t *pid, const float input);
```

| 参数 | 说明 |
|------|------|
| `pid` | PID 对象指针 |
| `input` | 当前测量值（反馈值） |
| **返回值** | PID 输出（已限幅） |

**位置式 PID 计算公式**：
```
error = target - input
sum_error += error                          // 积分累加
output = Kp·error + Ki·sum_error + Kd·(error - prev_error) + Kf·target
```

**增量式 PID 计算公式**：
```
error = target - input
output += Kp·(error - prev_error) + Ki·error + Kd·(error - 2·prev_error + pre_error)
```

**限幅逻辑**（两种类型共用）：
- 积分限幅：`sum_error` 被钳位在 `[sum_error_limit_n, sum_error_limit_p]`
- 输出限幅：`output` 被钳位在 `[output_limit_n, output_limit_p]`

### 7. PID_Reset()

**功能**：重置 PID 内部状态（清零误差、积分、输出）。

```c
void PID_Reset(PID_t *pid);
```

| 参数 | 说明 |
|------|------|
| `pid` | PID 对象指针 |

> `Stop_Car()` 中会调用此函数清除积分历史，防止停车后重新启动时积分积累导致突然冲出。

### 8. PID_GetOutput()

**功能**：获取当前 PID 输出值。

```c
float PID_GetOutput(const PID_t *pid);
```

| 参数 | 说明 |
|------|------|
| `pid` | PID 对象指针（const） |
| **返回值** | 当前输出值 |

### 9. PID_GetCurrentError()

**功能**：计算当前偏差值。

```c
float PID_GetCurrentError(const PID_t *pid, const float current_value);
```

| 参数 | 说明 |
|------|------|
| `pid` | PID 对象指针（const） |
| `current_value` | 当前测量值 |
| **返回值** | `target - current_value` |

## 本项目的 PID 使用

### 速度环（10ms 周期，在 TIMA0 中断中执行）

```
目标速度（由循迹/角度环设定）
  → PID_SetTarget(&pidSpeed, targetSpeed)
  → PID_Calc(&pidSpeed, Encoder_GetSpeedLine())
  → Motor_SetSpeed(output)  // 输出 PWM
```

### 循迹环（10ms 周期，在主循环中执行）

```
灰度质心偏差（0 为中心）
  → PID_Calc(&pidLine, line_offset)
  → 输出 = 转向量（加到左右轮目标速度）
```

### 角度环（10ms 周期，在主循环中执行）

```
Yaw 目标角度（如 90° 转弯）
  → PID_Calc(&pidAngle, current_yaw)
  → 输出 = 转向微调量（叠加在圆弧差速基准上）
```
