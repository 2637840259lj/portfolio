////////////////////////////////////////
// 文件名称: led.h
// 文件描述: LED驱动接口 - 通用HAL设计
// 文件版本: V1.0
// 修改记录:
//   1. (2026-03-03) 创建文件
////////////////////////////////////////


#ifndef __LED_H__
#define __LED_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ti_msp_dl_config.h"

// LED状态定义
#define LED_OFF     0  // 灭
#define LED_ON      1  // 亮

// LED最大数量
#define LED_MAX     3


/* LED GPIO配置结构体 */
typedef struct
{
    void *port;         // GPIO端口基地址 (例如 MSPM0 的 GPIO_Regs* 或 STM32 的 GPIO_TypeDef*)
    uint32_t pin;       // GPIO引脚掩码/编号 (例如 MSPM0 的 DL_GPIO_PIN_x 或 STM32 的 GPIO_PIN_x)
    uint8_t active_level;  // 激活电平 (0=低电平有效, 1=高电平有效)
} led_gpio_t;


/* LED设备配置结构体 */
typedef struct
{
    led_gpio_t gpio;
    uint8_t state;      // 当前状态
} led_device_t;


// 预定义板载LED (MSPM0G3507 LaunchPad)
extern led_device_t led_red;
extern led_device_t led_green;
extern led_device_t led_blue;


/***************************************************************
 * 函数名称: led_init
 * 说    明: LED驱动初始化
 * 参    数: led - LED设备指针
 * 返 回 值: 无
 ***************************************************************/
void led_init(led_device_t *led);

/***************************************************************
 * 函数名称: led_on
 * 说    明: LED点亮
 * 参    数: led - LED设备指针
 * 返 回 值: 无
 ***************************************************************/
void led_on(led_device_t *led);

/***************************************************************
 * 函数名称: led_off
 * 说    明: LED熄灭
 * 参    数: led - LED设备指针
 * 返 回 值: 无
 ***************************************************************/
void led_off(led_device_t *led);

/***************************************************************
 * 函数名称: led_toggle
 * 说    明: LED翻转
 * 参    数: led - LED设备指针
 * 返 回 值: 无
 ***************************************************************/
void led_toggle(led_device_t *led);

/***************************************************************
 * 函数名称: led_get_state
 * 说    明: 获取LED状态
 * 参    数: led - LED设备指针
 * 返 回 值: LED状态 (LED_ON=1, LED_OFF=0)
 ***************************************************************/
uint8_t led_get_state(led_device_t *led);

/***************************************************************
 * 函数名称: led_set_state
 * 说    明: 设置LED状态
 * 参    数: led   - LED设备指针
 *           state - LED状态 (LED_ON or LED_OFF)
 * 返 回 值: 无
 ***************************************************************/
void led_set_state(led_device_t *led, uint8_t state);


#endif

