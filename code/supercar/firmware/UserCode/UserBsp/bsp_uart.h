#ifndef __BSP_UART_H
#define __BSP_UART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "ti_msp_dl_config.h"

void uart0_send_char(char ch);
void uart0_send_string(char* str);

int UART1_Printf(const char *format, ...);

void DebugUartInit(void);

#endif  // __BSP_UART_H