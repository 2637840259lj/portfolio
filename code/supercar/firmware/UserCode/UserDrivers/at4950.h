#ifndef __AT4950_H
#define __AT4950_H

#include "ti_msp_dl_config.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MOTOR_Config {
  GPTIMER_Regs *htim1;
  DL_TIMER_CC_INDEX channel1;
  GPTIMER_Regs *htim2;
  DL_TIMER_CC_INDEX channel2;
  uint8_t reverse; // 电机的方向是否反转。0-正常，1-反转
} MOTOR_Config;

// 电机结构体
typedef struct MOTOR {
  MOTOR_Config config;
  int16_t speed;
} MOTOR_t;

void Motor_ConfigInit(MOTOR_t *motor, GPTIMER_Regs *htim1, DL_TIMER_CC_INDEX channel1, 
    GPTIMER_Regs *htim2, DL_TIMER_CC_INDEX channel2, uint8_t reverse);

void Motor_SetSpeed(MOTOR_t *motor, int16_t speed);

void Motor_Stop(MOTOR_t *motor);

void Motor_Brake(MOTOR_t *motor);

#ifdef __cplusplus
}
#endif

#endif  // __AT4950_H
