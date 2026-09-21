

#include "isr.h"
#include "encoder_driver.h"
#include "pid.h"
#include "at4950.h"
#include "ble.h"
#include "ble_param.h"
#include "app_ball_vision.h"

#include <stdio.h>

extern Encoder_t encoderLeft;
extern Encoder_t encoderRight;

extern PID_t pidLeftSpeed;
extern PID_t pidRightSpeed;
extern MOTOR_t motorLeft;
extern MOTOR_t motorRight;
extern float targetSpeedLeft;
extern float targetSpeedRight;

/* 位置保持模式标志 (app_debug.c 控制) */
extern bool g_poshold_active;

void TIMA0_IRQHandler(void)
{
    if (DL_TimerA_getPendingInterrupt(TIMER_0_INST) == DL_TIMER_IIDX_ZERO)
    {
        Encoder_Update(&encoderLeft);
        Encoder_Update(&encoderRight);

        /* 位置保持模式: 跳过速度PID, 由调试模块直接设PWM */
        if (g_poshold_active) return;

        // 根据当前目标速度在 MCU 本地插值速度环参数；App 不参与该 10ms 闭环。
        float currentSpeedLeft = Encoder_GetSpeedLine(&encoderLeft);
        float currentSpeedRight = Encoder_GetSpeedLine(&encoderRight);
        float scheduleSpeed = targetSpeedLeft >= 0.0f ? targetSpeedLeft : -targetSpeedLeft;
        float rightAbs = targetSpeedRight >= 0.0f ? targetSpeedRight : -targetSpeedRight;
        pid_schedule_node_t gains;
        if (rightAbs > scheduleSpeed) scheduleSpeed = rightAbs;
        BLE_Param_InterpolatePidSchedule(scheduleSpeed, &gains);
        pidLeftSpeed.kp = pidRightSpeed.kp = gains.speed_kp;
        pidLeftSpeed.ki = pidRightSpeed.ki = gains.speed_ki;
        pidLeftSpeed.kd = pidRightSpeed.kd = gains.speed_kd;
        pidLeftSpeed.kf = pidRightSpeed.kf = gains.speed_kf;

        // 计算PID输出
        float outLeft = PID_Calc(&pidLeftSpeed, currentSpeedLeft);
        float outRight = PID_Calc(&pidRightSpeed, currentSpeedRight);

        // 设置电机PWM
        Motor_SetSpeed(&motorLeft, (int16_t)outLeft);
        Motor_SetSpeed(&motorRight, (int16_t)outRight);
    }
}

// void TIMA1_IRQHandler (void)
//{

//}

// void TIMG6_IRQHandler (void)
//{

//}

// void TIMG7_IRQHandler (void)
//{
// }

// void TIMG8_IRQHandler (void)
//{
// }

// void TIMG12_IRQHandler (void)
//{
// }

// void UART0_IRQHandler (void)
//{
//	switch(DL_UART_getPendingInterrupt(UART0))
//	{
//		case DL_UART_IIDX_TX:
//         {
//             uart_callback_list[0](UART_INTERRUPT_STATE_TX, uart_callback_ptr_list[0]);
//         }break;
//		case DL_UART_IIDX_RX:
//         {
//             uart_callback_list[0](UART_INTERRUPT_STATE_RX, uart_callback_ptr_list[0]);
// #if DEBUG_UART_USE_INTERRUPT
//                 debug_interrupr_handler();
// #endif
//         }break;

//		default:    break;
//	}
//    DL_UART_clearInterruptStatus(UART0, UART0->CPU_INT.RIS);
//}

// void UART1_IRQHandler (void)
//{
//	switch(DL_UART_getPendingInterrupt(UART1))
//	{
//		case DL_UART_IIDX_TX:
//         {
//             uart_callback_list[1](UART_INTERRUPT_STATE_TX, uart_callback_ptr_list[1]);
//         }break;
//		case DL_UART_IIDX_RX:
//         {
//             uart_callback_list[1](UART_INTERRUPT_STATE_RX, uart_callback_ptr_list[1]);
//
//					 //wifi_uart_callback();
//
//			wireless_module_uart_handler();                 // ??????????
//
//         }break;

//		default:    break;
//	}
//    DL_UART_clearInterruptStatus(UART1, UART1->CPU_INT.RIS);
//}

// void UART2_IRQHandler (void)
//{
//	switch(DL_UART_getPendingInterrupt(UART2))
//	{
//		case DL_UART_IIDX_TX:
//         {
//             uart_callback_list[2](UART_INTERRUPT_STATE_TX, uart_callback_ptr_list[2]);
//         }break;
//		case DL_UART_IIDX_RX:
//         {
//             uart_callback_list[2](UART_INTERRUPT_STATE_RX, uart_callback_ptr_list[2]);
//         }break;

//		default:    break;
//	}
//    DL_UART_clearInterruptStatus(UART2, UART2->CPU_INT.RIS);
//}

// void UART3_IRQHandler (void)
//{
//	switch(DL_UART_getPendingInterrupt(UART3))
//	{
//		case DL_UART_IIDX_TX:
//         {
//             uart_callback_list[3](UART_INTERRUPT_STATE_TX, uart_callback_ptr_list[3]);
//         }break;
//		case DL_UART_IIDX_RX:
//         {
//             uart_callback_list[3](UART_INTERRUPT_STATE_RX, uart_callback_ptr_list[3]);
//         }break;

//		default:    break;
//	}
//    DL_UART_clearInterruptStatus(UART3, UART3->CPU_INT.RIS);
//}

void UART3_IRQHandler(void)
{
    switch (DL_UART_getPendingInterrupt(UART_BLE_INST)) {
        case DL_UART_IIDX_TX:
            BLE_TxIrqHandler();
            break;
        case DL_UART_IIDX_RX:
            while (!DL_UART_isRXFIFOEmpty(UART_BLE_INST)) {
                uint8_t data = DL_UART_Main_receiveData(UART_BLE_INST);
                BLE_RxBufferEnqueue(data);
            }
            break;
        default:
            break;
    }
    DL_UART_clearInterruptStatus(UART_BLE_INST, DL_UART_INTERRUPT_RX | DL_UART_INTERRUPT_TX);
}

/* ── UART2 (UART_K230, PB15=TX/PB16=RX) — K230 球位置数据接收 ── */
void UART2_IRQHandler(void)
{
    uint32_t iidx = DL_UART_Main_getPendingInterrupt(UART_K230_INST);

    switch (iidx) {
        case DL_UART_MAIN_IIDX_RX:
            while (!DL_UART_isRXFIFOEmpty(UART_K230_INST)) {
                BallVision_OnRxByte(DL_UART_Main_receiveData(UART_K230_INST));
            }
            break;

        case DL_UART_MAIN_IIDX_OVERRUN_ERROR:
        case DL_UART_MAIN_IIDX_BREAK_ERROR:
        case DL_UART_MAIN_IIDX_PARITY_ERROR:
        case DL_UART_MAIN_IIDX_FRAMING_ERROR:
        case DL_UART_MAIN_IIDX_NOISE_ERROR:
            /* 半帧已不可信，丢弃并清除所有已启用的错误源。 */
            BallVision_OnRxError();
            DL_UART_Main_clearInterruptStatus(
                UART_K230_INST,
                DL_UART_MAIN_INTERRUPT_OVERRUN_ERROR |
                DL_UART_MAIN_INTERRUPT_BREAK_ERROR |
                DL_UART_MAIN_INTERRUPT_PARITY_ERROR |
                DL_UART_MAIN_INTERRUPT_FRAMING_ERROR |
                DL_UART_MAIN_INTERRUPT_NOISE_ERROR);
            break;

        default:
            break;
    }
}

void UART0_IRQHandler(void)
{
    /* 轮询模式: 中断不做任何事, 由主循环 10ms 轮询读取 */
}

void GROUP1_IRQHandler(void)
{
    /* 1. 将引脚掩码静态常量化，避免每次进入中断都进行位运算计算 */
    static const uint32_t leftPins = GPIO_ENCODER_LA_PIN | GPIO_ENCODER_LB_PIN;
    static const uint32_t rightPins = GPIO_ENCODER_RA_PIN | GPIO_ENCODER_RB_PIN;
    static const uint32_t allPins = leftPins | rightPins;

    /* 读取已使能的中断状态，区分左右编码器 */
    uint32_t GpioState = DL_GPIO_getEnabledInterruptStatus(GPIO_ENCODER_PORT, allPins);

    if (GpioState & leftPins)
    {
        /* 左编码器：读取 A/B 相并计算当前 2bit 状态 */
        uint32_t pins = DL_GPIO_readPins(GPIO_ENCODER_PORT, leftPins);

        /* 2. 消除三目运算符分支，利用 !! 逻辑规整(0或1)直接移位，提升执行速度 */
        uint8_t curr = ((!!(pins & GPIO_ENCODER_LA_PIN)) << 1) | (!!(pins & GPIO_ENCODER_LB_PIN));

        /* 3. 去掉 s_leftPrev != 0xFF 的判断，容忍上电最多1个脉冲误差换取每次中断少一个分支 */
        encoderLeft.temp_count += Encoder_QuadTable[(encoderLeft.prev_state << 2) | curr];
        encoderLeft.prev_state = curr;
    }

    if (GpioState & rightPins)
    {
        /* 右编码器：读取 A/B 相并计算当前 2bit 状态 */
        uint32_t pins = DL_GPIO_readPins(GPIO_ENCODER_PORT, rightPins);

        uint8_t curr = ((!!(pins & GPIO_ENCODER_RA_PIN)) << 1) | (!!(pins & GPIO_ENCODER_RB_PIN));

        encoderRight.temp_count += Encoder_QuadTable[(encoderRight.prev_state << 2) | curr];
        encoderRight.prev_state = curr;
    }

    /* 4. 合并清除中断标志，避免调用两次底层库函数 */
    DL_GPIO_clearInterruptStatus(GPIO_ENCODER_PORT, GpioState & allPins);
}
