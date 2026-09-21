#include "bsp_uart.h"

#include "stdio.h"

#if !defined(__MICROLIB)
// 不使用微库的话就需要添加下面的函数
#if (__ARMCLIB_VERSION <= 6000000)
// 如果编译器是AC5  就定义下面这个结构体
struct __FILE
{
    int handle;
};
#endif
FILE __stdout;
// 定义_sys_exit()以避免使用半主机模式
void _sys_exit(int x)
{
    x = x;
}
#endif

int fputc(int ch, FILE *stream)
{
    // printf 映射到 UART_1 (PA8=TX, PA9=RX), 匹配板载调试串口
    while (DL_UART_isBusy(UART_1_INST) == true)
        ;

    DL_UART_Main_transmitData(UART_1_INST, ch);

    return ch;
}

#include <stdarg.h>
// 专门用于向 UART1 输出的 printf，用于 VOFA+ FireWater
int UART1_Printf(const char *format, ...)
{
    char buffer[256];
    va_list args;
    int len, i;

    va_start(args, format);
    len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    for (i = 0; i < len; i++)
    {
        while (DL_UART_isBusy(UART_1_INST) == true)
            ;
        DL_UART_Main_transmitData(UART_1_INST, buffer[i]);
    }

    return len;
}

void DebugUartInit(void)
{
    /* 清除 BLE 串口中断标志 */
    NVIC_ClearPendingIRQ(UART_BLE_INST_INT_IRQN);
    /* 使能 BLE 串口中断 */
    NVIC_EnableIRQ(UART_BLE_INST_INT_IRQN);
}

/* BLE UART 发送单个字符 */
void uart0_send_char(char ch)
{
    while (DL_UART_isBusy(UART_BLE_INST) == true)
        ;
    DL_UART_Main_transmitData(UART_BLE_INST, ch);
}
// 串口发送字符串
void uart0_send_string(char *str)
{
    // 当前字符串地址不在结尾 并且 字符串首地址不为空
    while (*str != 0 && str != 0)
    {
        // 发送字符串首地址中的字符，并且在发送完成之后首地址自增
        uart0_send_char(*str++);
    }
}
