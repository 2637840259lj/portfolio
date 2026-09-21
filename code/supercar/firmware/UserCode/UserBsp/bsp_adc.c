/**
 * @file bsp_adc.c
 * @brief ADC 板级支持包底层驱动
 * @note 该文件提供基于 MSPM0 ADC12 外设的数据采集功能
 */

#include "bsp_adc.h"

/* 全局变量静态化，避免污染全局命名空间，提升模块封装性 */
static volatile bool gCheckADC = false;
static volatile unsigned int gAdcResult = 0;

/**
 * @brief 初始化 ADC 中断配置及状态
 */
void bsp_adc_init(void)
{
    /* 确保初始状态为未转换完成 */
    gCheckADC = false;
    gAdcResult = 0;

    /*
     * 开启外设对应的中断使能
     * (底层硬件管脚配置和时钟树初始化交由 SYSCFG_DL_init 处理)
     */
    NVIC_EnableIRQ(ADC12_0_INST_INT_IRQN);
}

/**
 * @brief 获取 ADC 采样结果
 * @return 采样值（通常为 12 位，范围 0~4095）
 */
unsigned int adc_getValue(void)
{
    /* 每次启动转换前重置完成标志，防止读取旧数据 */
    gCheckADC = false;

    /* 触发 ADC 软件转换 */
    DL_ADC12_startConversion(ADC12_0_INST);

    /* 等待中断标志置位（WFE：Wait For Event 可降低等待期间的功耗） */
    while (false == gCheckADC)
    {
        __WFE();
    }

    /* 从内存结果寄存器 3 中获取转换结果 (MEM3) */
    gAdcResult = DL_ADC12_getMemResult(ADC12_0_INST, ADC12_0_ADCMEM_3);

    /* 获取完毕后再次复位标志位，保证逻辑闭环 */
    gCheckADC = false;

    return gAdcResult;
}

/**
 * @brief ADC12_0 中断服务函数
 * @note 当 ADC 转换完成后，通过中断将标志位置 true 以唤醒主循环
 */
void ADC0_IRQHandler(void)
{
    /* 读取并判断具体的 ADC 中断源 */
    switch (DL_ADC12_getPendingInterrupt(ADC12_0_INST))
    {
        case DL_ADC12_IIDX_MEM3_RESULT_LOADED:
            /* 转换结果加载完成，置位完成标志 */
            gCheckADC = true;
            break;

        default:
            break;
    }
}
