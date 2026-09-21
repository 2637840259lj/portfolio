#ifndef __SYS_TIME_H
#define __SYS_TIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ti_msp_dl_config.h"

extern volatile uint32_t g_system_ticks_ms;

void delay_us(uint32_t us);

int mspm0_delay_ms(uint32_t num_ms);
uint32_t mspm0_get_clock_ms(void);
void SysTick_Init(void);

#endif  // __SYS_TIME_H