

#ifndef __BUZZER_H
#define __BUZZER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ti_msp_dl_config.h"


// 蜂鸣器状态定义
#define BUZZER_OFF      0  // 关闭
#define BUZZER_ON       1  // 启动


/* 蜂鸣器GPIO配置结构体 */
typedef struct
{
    void *port;             // GPIO端口基地址 (例如 MSPM0 的 GPIO_Regs* 或 STM32 的 GPIO_TypeDef*)
    uint32_t pin;           // GPIO引脚掩码/编号 (例如 MSPM0 的 DL_GPIO_PIN_x 或 STM32 的 GPIO_PIN_x)
    uint8_t active_level;   // 激活电平 (0=低电平有效, 1=高电平有效)
} buzzer_gpio_t;


/* 蜂鸣器设备配置结构体 */
typedef struct
{
    buzzer_gpio_t gpio;
    uint8_t state;          // 当前状态
} buzzer_device_t;


// 预定义蜂鸣器实例 (MSPM0G3507 LaunchPad)
extern buzzer_device_t buzzer0;


/***************************************************************
 * 函数名称: buzzer_init
 * 说    明: 蜂鸣器驱动初始化
 * 参    数: buzzer - 蜂鸣器设备指针
 * 返 回 值: 无
 ***************************************************************/
void buzzer_init(buzzer_device_t *buzzer);

/***************************************************************
 * 函数名称: buzzer_on
 * 说    明: 蜂鸣器启动
 * 参    数: buzzer - 蜂鸣器设备指针
 * 返 回 值: 无
 ***************************************************************/
void buzzer_on(buzzer_device_t *buzzer);

/***************************************************************
 * 函数名称: buzzer_off
 * 说    明: 蜂鸣器关闭
 * 参    数: buzzer - 蜂鸣器设备指针
 * 返 回 值: 无
 ***************************************************************/
void buzzer_off(buzzer_device_t *buzzer);

/***************************************************************
 * 函数名称: buzzer_get_state
 * 说    明: 获取蜂鸣器状态
 * 参    数: buzzer - 蜂鸣器设备指针
 * 返 回 值: 蜂鸣器状态 (BUZZER_ON=1, BUZZER_OFF=0)
 ***************************************************************/
uint8_t buzzer_get_state(buzzer_device_t *buzzer);

/***************************************************************
 * 函数名称: buzzer_set_state
 * 说    明: 设置蜂鸣器状态
 * 参    数: buzzer - 蜂鸣器设备指针
 *           state  - 蜂鸣器状态 (BUZZER_ON or BUZZER_OFF)
 * 返 回 值: 无
 ***************************************************************/
void buzzer_set_state(buzzer_device_t *buzzer, uint8_t state);


#endif  // __BUZZER_H

