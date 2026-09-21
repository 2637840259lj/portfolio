# LED 驱动模块

## 概述

通用 LED 驱动模块，基于 MSPM0 DriverLib 实现，支持低电平/高电平有效可配置，提供开关、翻转和状态查询等基本操作。本项目预定义了三个板载 RGB LED 实例：`led_red`、`led_green`、`led_blue`。

## 硬件引脚映射

| 功能 | 信号 | MCU 引脚 | 有效电平 |
|------|------|----------|----------|
| 红色 LED | LED_R | PB26 | 低电平有效 |
| 绿色 LED | LED_G | PB22 | 低电平有效 |
| 蓝色 LED | LED_B | PB27 | 低电平有效 |

> 所有 LED 引脚已在 SYSCFG 中配置为数字输出，初始状态为低电平（灭）。

## 数据结构

### led_gpio_t

```c
typedef struct {
    void *port;            // GPIO 端口基地址（GPIOA/GPIOB 等）
    uint32_t pin;          // GPIO 引脚掩码（DL_GPIO_PIN_x）
    uint8_t active_level;  // 激活电平：LED_ON(1)=高有效, LED_OFF(0)=低有效
} led_gpio_t;
```

### led_device_t

```c
typedef struct {
    led_gpio_t gpio;  // GPIO 配置
    uint8_t state;     // 当前状态（LED_ON 或 LED_OFF）
} led_device_t;
```

### 预定义 LED 实例

| 变量名 | 端口 | 引脚 | 有效电平 |
|--------|------|------|----------|
| `led_red` | GPIOB | PB26 | 低有效 |
| `led_green` | GPIOB | PB22 | 低有效 |
| `led_blue` | GPIOB | PB27 | 低有效 |

### 状态常量

```c
#define LED_OFF  0  // 灭
#define LED_ON   1  // 亮
#define LED_MAX  3  // LED 最大数量
```

## API 函数详解

### 1. led_init()

**功能**：初始化 LED。将状态重置为灭，GPIO 配置已由 SYSCFG 处理。

```c
void led_init(led_device_t *led);
```

| 参数 | 说明 |
|------|------|
| `led` | LED 设备指针 |

### 2. led_on()

**功能**：点亮 LED。根据 `active_level` 自动选择正确的 GPIO 电平。

```c
void led_on(led_device_t *led);
```

| 参数 | 说明 |
|------|------|
| `led` | LED 设备指针 |

### 3. led_off()

**功能**：熄灭 LED。

```c
void led_off(led_device_t *led);
```

| 参数 | 说明 |
|------|------|
| `led` | LED 设备指针 |

### 4. led_toggle()

**功能**：翻转 LED 状态。

```c
void led_toggle(led_device_t *led);
```

| 参数 | 说明 |
|------|------|
| `led` | LED 设备指针 |

### 5. led_get_state()

**功能**：获取当前 LED 状态。

```c
uint8_t led_get_state(led_device_t *led);
```

| 参数 | 说明 |
|------|------|
| `led` | LED 设备指针 |
| **返回值** | `LED_ON`(1) 或 `LED_OFF`(0) |

### 6. led_set_state()

**功能**：设置 LED 状态。

```c
void led_set_state(led_device_t *led, uint8_t state);
```

| 参数 | 说明 |
|------|------|
| `led` | LED 设备指针 |
| `state` | `LED_ON`(1) 或 `LED_OFF`(0) |

## 引用关系

- **main() 初始化**：调用 `led_init()` 依次初始化红绿蓝三个 LED，然后闪烁三色用于上电自检。
- **app_indicator**：管理运行状态指示——任务运行时亮绿灯、停止时亮红灯、支持红灯闪烁。
