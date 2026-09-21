////////////////////////////////////////
// 文件名称: led.c
// 文件描述: LED驱动实现 - 基于MSPM0 DriverLib
// 文件版本: V1.1
// 修改记录:
//   1. (2026-03-03) 创建文件
//   2. (2026-03-26) 适配 MSPM0 DriverLib
////////////////////////////////////////

#include "led.h"

// 预定义板载LED实例 (MSPM0G3507 LaunchPad)
led_device_t led_red = {
    .gpio = {.port = GPIO_LED_PORT, .pin = GPIO_LED_R_PIN, .active_level = LED_OFF}, .state = LED_OFF};

led_device_t led_green = {
    .gpio = {.port = GPIO_LED_PORT, .pin = GPIO_LED_G_PIN, .active_level = LED_OFF}, .state = LED_OFF};

led_device_t led_blue = {
    .gpio = {.port = GPIO_LED_PORT, .pin = GPIO_LED_B_PIN, .active_level = LED_OFF}, .state = LED_OFF};

/***************************************************************
 * 函数名称: led_init
 * 说    明: LED驱动初始化
 * 参    数: led - LED设备指针
 * 返 回 值: 无
 ***************************************************************/
void led_init(led_device_t *led)
{
    if (led == NULL)
        return;

    // GPIO已经在 SYSCFG_DL_GPIO_init() 中初始化 (由 empty.syscfg 生成)
    // 这里只初始化LED状态
    led->state = LED_OFF;
    led_off(led);
}

/***************************************************************
 * 函数名称: led_on
 * 说    明: LED点亮
 * 参    数: led - LED设备指针
 * 返 回 值: 无
 ***************************************************************/
void led_on(led_device_t *led)
{
    if (led == NULL)
        return;

    // 根据激活电平设置GPIO
    if (led->gpio.active_level == LED_ON)
    {
        DL_GPIO_setPins((GPIO_Regs *)led->gpio.port, led->gpio.pin);
    }
    else
    {
        DL_GPIO_clearPins((GPIO_Regs *)led->gpio.port, led->gpio.pin);
    }
    led->state = LED_ON;
}

/***************************************************************
 * 函数名称: led_off
 * 说    明: LED熄灭
 * 参    数: led - LED设备指针
 * 返 回 值: 无
 ***************************************************************/
void led_off(led_device_t *led)
{
    if (led == NULL)
        return;

    // 根据激活电平反向设置GPIO
    if (led->gpio.active_level == LED_ON)
    {
        DL_GPIO_clearPins((GPIO_Regs *)led->gpio.port, led->gpio.pin);
    }
    else
    {
        DL_GPIO_setPins((GPIO_Regs *)led->gpio.port, led->gpio.pin);
    }
    led->state = LED_OFF;
}

/***************************************************************
 * 函数名称: led_toggle
 * 说    明: LED翻转
 * 参    数: led - LED设备指针
 * 返 回 值: 无
 ***************************************************************/
void led_toggle(led_device_t *led)
{
    if (led == NULL)
        return;

    DL_GPIO_togglePins((GPIO_Regs *)led->gpio.port, led->gpio.pin);
    led->state = (led->state == LED_ON) ? LED_OFF : LED_ON;
}

/***************************************************************
 * 函数名称: led_get_state
 * 说    明: 获取LED状态
 * 参    数: led - LED设备指针
 * 返 回 值: LED状态 (LED_ON=1, LED_OFF=0)
 ***************************************************************/
uint8_t led_get_state(led_device_t *led)
{
    if (led == NULL)
        return LED_OFF;

    return led->state;
}

/***************************************************************
 * 函数名称: led_set_state
 * 说    明: 设置LED状态
 * 参    数: led   - LED设备指针
 *           state - LED状态 (LED_ON or LED_OFF)
 * 返 回 值: 无
 ***************************************************************/
void led_set_state(led_device_t *led, uint8_t state)
{
    if (led == NULL)
        return;

    if (state == LED_ON)
        led_on(led);
    else
        led_off(led);
}
