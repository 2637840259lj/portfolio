# ADC 板级支持包模块（BSP ADC）

## 概述

本模块封装 MSPM0 ADC12 外设的底层驱动，为灰度传感器提供单通道模拟量采集接口。ADC 工作在 12bit 分辨率、自动重复模式，通过软件触发 + WFE 低功耗等待实现阻塞式采样。

## 硬件引脚映射

| 功能 | 信号 | MCU 引脚 | 外设通道 |
|------|------|----------|----------|
| ADC 模拟输入 | ADC_CH3 | PA24 | ADC0 CH3 |

## ADC 配置参数

| 参数 | 值 |
|------|-----|
| ADC 外设 | ADC0 |
| 采样通道 | DL_ADC12_INPUT_CHAN_3 |
| 分辨率 | 12 bit（0~4095） |
| 参考电压 | VDDA（3.3V） |
| 时钟源 | SYSOSC（32MHz，÷8 = 4MHz） |
| 采样时间 | 2μs |
| 工作模式 | 自动重复 + 软件触发 |
| 数据格式 | 无符号整数 |
| 中断源 | MEM3_RESULT_LOADED |
| 中断优先级 | 0（最高） |

## 全局变量

```c
static volatile bool gCheckADC = false;         // ADC 转换完成标志
static volatile unsigned int gAdcResult = 0;    // ADC 转换结果缓存
```

> 两个变量均为 `static + volatile`，确保模块封装性和中断安全。

## API 函数详解

### 1. bsp_adc_init()

**功能**：初始化 ADC 中断配置。外设底层引脚和时钟已由 SYSCFG 处理。

```c
void bsp_adc_init(void);
```

**内部操作**：
- 重置 `gCheckADC` 和 `gAdcResult`
- 使能 `ADC12_0_INST_INT_IRQN` 中断

### 2. adc_getValue()

**功能**：触发单次 ADC 转换，阻塞等待完成，返回采样结果。

```c
unsigned int adc_getValue(void);
```

| 返回值 | 说明 |
|--------|------|
| unsigned int | 12 位 ADC 采样值（0~4095） |

**内部流程**：
1. 清零 `gCheckADC`
2. 触发软件转换 `DL_ADC12_startConversion()`
3. `__WFE()` 低功耗等待中断唤醒
4. 从中断标志唤醒后读取 MEM3 结果
5. 再次清零标志位
6. 返回采样值

> **注意**：`adc_getValue()` 是阻塞调用，每次转换约耗时数十微秒。灰度传感器中每个通道采样 8 次取平均，8 通道共需约 64 次 ADC 采样。

### 3. ADC0_IRQHandler()（同文件定义）

**功能**：ADC 转换完成中断服务函数。

```c
void ADC0_IRQHandler(void);
```

**中断条件**：`DL_ADC12_IIDX_MEM3_RESULT_LOADED`

**操作**：置位 `gCheckADC = true`，唤醒 `adc_getValue()` 中等待的 `__WFE()`。

## 引用关系

```
用户代码
  → adc_getValue()                     // 阻塞式采样

灰度传感器驱动 (No_Mcu_Ganv_Grayscale_Sensor.c)
  → Get_adc_of_user() → adc_getValue()  // 每通道 8 次采样
  → main() → NVIC_EnableIRQ(ADC12_0_INST_INT_IRQN)
```

ADC 外设由 SysConfig 自动生成配置（`SYSCFG_DL_ADC12_0_init()`），在 `SYSCFG_DL_init()` 中统一调用。
