#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#include "ti_msp_dl_config.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ADC 模块初始化
 * @note 此函数主要用于使能 ADC 相关的中断等状态复位。
 *       外设的引脚和时钟等底层初始化通常由 SysConfig 自动生成的代码 (SYSCFG_DL_init) 负责。
 */
void bsp_adc_init(void);

/**
 * @brief 触发单次转换并获取 ADC 采样结果
 * @note 此函数为阻塞式调用，会在触发转换后进入低功耗状态 (__WFE) 
 *       等待 ADC 转换完成的中断唤醒。
 * @return unsigned int ADC 通道 3 的采样结果
 */
unsigned int adc_getValue(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_ADC_H */