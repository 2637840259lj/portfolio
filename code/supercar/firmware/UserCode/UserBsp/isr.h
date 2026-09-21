
#ifndef _isr_h_
#define _isr_h_

#include "ti_msp_dl_config.h"
#include <stdint.h>

/* UART3 用于 ESP-01S 控制链路；UART2 用于 K230 球位置上报。 */
void UART3_IRQHandler(void);
void UART2_IRQHandler(void);
void UART0_IRQHandler(void);

#endif
