#include "sys_time.h"
#include "app_stepper_test.h"
#if STEPPER_TEST_MODE_ENABLE
#include "tmc2209_driver.h"
#endif

volatile uint32_t g_system_ticks_ms = 0;
volatile uint32_t start_time;

void delay_us(uint32_t us)
{
    // 根据实际测试调整此值
    // 80MHz下大约需要 (us * 80) 次循环（需校准）
    volatile uint32_t count = us * 80;
    while (count--)
        ;
}

int mspm0_delay_ms(uint32_t num_ms)
{
    start_time = g_system_ticks_ms;
    while (g_system_ticks_ms - start_time < num_ms)
        ;
    return 0;
}

uint32_t mspm0_get_clock_ms(void)
{
    return g_system_ticks_ms;
}

void SysTick_Init(void)
{
    DL_SYSTICK_config(CPUCLK_FREQ / 1000);
    NVIC_SetPriority(SysTick_IRQn, 0);
}

void SysTick_Handler(void)
{
    g_system_ticks_ms++;
#if STEPPER_TEST_MODE_ENABLE
    /* Dedicated low-speed step scheduler. SysTick remains 1 ms for all existing modules. */
    tmc2209_tick_1ms();
#endif

    if (g_system_ticks_ms % 500 == 0)
    {
    }
}