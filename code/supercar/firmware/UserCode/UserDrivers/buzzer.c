

////////////////////////////////////////
// 文件名称: buzzer.c
// 文件描述: 蜂鸣器驱动实现 - 基于MSPM0 DriverLib
// 修改记录:
//   1. (2026-03-26) 适配 MSPM0 DriverLib
////////////////////////////////////////

#include "buzzer.h"
#include <stddef.h>

// 预定义蜂鸣器实例 (MSPM0G3507 LaunchPad)
buzzer_device_t buzzer0 = {
    .gpio = { .port = (void *)GPIO_PORT, .pin = GPIO_BUZZER_PIN, .active_level = BUZZER_ON },
    .state = BUZZER_OFF
};


/***************************************************************
 * 函数名称: buzzer_init
 * 说    明: 蜂鸣器驱动初始化
 * 参    数: buzzer - 蜂鸣器设备指针
 * 返 回 值: 无
 ***************************************************************/
void buzzer_init(buzzer_device_t *buzzer)
{
    if (buzzer == NULL)
        return;

    // GPIO已经在 SYSCFG_DL_GPIO_init() 中初始化 (由 empty.syscfg 生成)
    // 这里只初始化蜂鸣器状态
    buzzer->state = BUZZER_OFF;
    buzzer_off(buzzer);
}

/***************************************************************
 * 函数名称: buzzer_on
 * 说    明: 蜂鸣器启动
 * 参    数: buzzer - 蜂鸣器设备指针
 * 返 回 值: 无
 ***************************************************************/
void buzzer_on(buzzer_device_t *buzzer)
{
    if (buzzer == NULL)
        return;

    // 设置初始电平
    if (buzzer->gpio.active_level == BUZZER_ON) {
        DL_GPIO_setPins((GPIO_Regs *)buzzer->gpio.port, buzzer->gpio.pin);
    } else {
        DL_GPIO_clearPins((GPIO_Regs *)buzzer->gpio.port, buzzer->gpio.pin);
    }

    buzzer->state = BUZZER_ON;
}

/***************************************************************
 * 函数名称: buzzer_off
 * 说    明: 蜂鸣器关闭
 * 参    数: buzzer - 蜂鸣器设备指针
 * 返 回 值: 无
 ***************************************************************/
 void buzzer_off(buzzer_device_t *buzzer)
 {
     if (buzzer == NULL)
         return;
 
     // 设置关闭电平
     if (buzzer->gpio.active_level == BUZZER_ON) {
         DL_GPIO_clearPins((GPIO_Regs *)buzzer->gpio.port, buzzer->gpio.pin);
     } else {
         DL_GPIO_setPins((GPIO_Regs *)buzzer->gpio.port, buzzer->gpio.pin);
     }

     buzzer->state = BUZZER_OFF;
 }

/***************************************************************
 * 函数名称: buzzer_get_state
 * 说    明: 获取蜂鸣器状态
 * 参    数: buzzer - 蜂鸣器设备指针
 * 返 回 值: 蜂鸣器状态 (BUZZER_ON=1, BUZZER_OFF=0)
 ***************************************************************/
uint8_t buzzer_get_state(buzzer_device_t *buzzer)
{
    if (buzzer == NULL)
        return BUZZER_OFF;

    return buzzer->state;
}

/***************************************************************
 * 函数名称: buzzer_set_state
 * 说    明: 设置蜂鸣器状态
 * 参    数: buzzer - 蜂鸣器设备指针
 *           state  - 蜂鸣器状态 (BUZZER_ON or BUZZER_OFF)
 * 返 回 值: 无
 ***************************************************************/
void buzzer_set_state(buzzer_device_t *buzzer, uint8_t state)
{
    if (buzzer == NULL)
        return;

    if (state == BUZZER_ON)
        buzzer_on(buzzer);
    else
        buzzer_off(buzzer);
}

