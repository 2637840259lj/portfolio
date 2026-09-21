#include "at4950.h"

#define PWM_MAX 999

#define Motor_ABS(x) ((x) >= 0 ? (x) : -(x))

// 电机速度限幅
int Motor_Limit_Speed(MOTOR_t *motor, int16_t speed, int16_t max_speed, int16_t min_speed)
{
    if (speed > max_speed)
        speed = max_speed;
    else if (speed < min_speed)
        speed = min_speed;

    return speed;
}

// 电机配置初始化
void Motor_ConfigInit(MOTOR_t *motor,
    GPTIMER_Regs *htim1,
    DL_TIMER_CC_INDEX channel1,
    GPTIMER_Regs *htim2,
    DL_TIMER_CC_INDEX channel2,
    uint8_t reverse)
{

    motor->config.htim1 = htim1;
    motor->config.channel1 = channel1;

    motor->config.htim2 = htim2;
    motor->config.channel2 = channel2;

    motor->config.reverse = reverse;
    motor->speed = 0;

    DL_TimerG_startCounter(motor->config.htim1);
    DL_TimerG_startCounter(motor->config.htim2);
}

// 速度控制
void Motor_SetSpeed(MOTOR_t *motor, int16_t speed)
{

    // 电机速度限幅
    motor->speed = Motor_Limit_Speed(motor, speed, PWM_MAX, -PWM_MAX);

    // 根据方向和反转标志设置占空比
    if ((motor->speed > 0 && motor->config.reverse == 0) || (motor->speed < 0 && motor->config.reverse == 1))
    {
        // 正向旋转
        DL_TimerG_setCaptureCompareValue(motor->config.htim1, Motor_ABS(motor->speed), motor->config.channel1);
        DL_TimerG_setCaptureCompareValue(motor->config.htim2, 1, motor->config.channel2);
    }
    else if ((motor->speed < 0 && motor->config.reverse == 0) || (motor->speed > 0 && motor->config.reverse == 1))
    {
        // 反向旋转
        DL_TimerG_setCaptureCompareValue(motor->config.htim1, 1, motor->config.channel1);
        DL_TimerG_setCaptureCompareValue(motor->config.htim2, Motor_ABS(motor->speed), motor->config.channel2);
    }
    else
    {
        DL_TimerG_setCaptureCompareValue(motor->config.htim1, 999, motor->config.channel1);
        DL_TimerG_setCaptureCompareValue(motor->config.htim2, 999, motor->config.channel2);
    }
}

// 电机停止 (自由滑行)
// AT4950 / DRV8833 逻辑: IN1=0, IN2=0
// (对应PWM占空比都为0，此库中1代表低电平状态以防死区)
void Motor_Stop(MOTOR_t *motor)
{
    motor->speed = 0;
    DL_TimerG_setCaptureCompareValue(motor->config.htim1, 1, motor->config.channel1);
    DL_TimerG_setCaptureCompareValue(motor->config.htim2, 1, motor->config.channel2);
}

// 电机刹车 (快速制动)
// AT4950 / DRV8833 逻辑: IN1=1, IN2=1 (对应PWM占空比都为最大)
void Motor_Brake(MOTOR_t *motor)
{
    motor->speed = 0;
    DL_TimerG_setCaptureCompareValue(motor->config.htim1, PWM_MAX, motor->config.channel1);
    DL_TimerG_setCaptureCompareValue(motor->config.htim2, PWM_MAX, motor->config.channel2);
}
