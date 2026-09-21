#include "encoder_driver.h"
#include "ti_msp_dl_config.h"

const int8_t Encoder_QuadTable[16] = {
    0,  1, -1, 0,
   -1,  0,  0, 1,
    1,  0,  0,-1,
    0, -1,  1, 0
};

void Encoder_Driver_Init(Encoder_t *encoder, unsigned char reverse)
{

    // 初始化数据结构
    encoder->reverse = reverse;
    encoder->count = 0;
    encoder->prev_state = 0;
    encoder->total_count = 0;
    encoder->speed_rpm = 0.0f;
    encoder->speed_cm_s = 0.0f;

    NVIC_EnableIRQ(GPIOB_INT_IRQn);
}

void Encoder_Update(Encoder_t *encoder)
{
    // 1. 关中断读取并清零，保证原子性操作，防止在这两行代码之间来中断导致漏掉脉冲！
    __disable_irq();
    int32_t raw_count = encoder->temp_count; // 取出中断中累计的脉冲增量
    encoder->temp_count = 0;                 // 清零计数器，为下个周期做准备
    __enable_irq();

    // 2. 处理编码器反向
    encoder->count = (encoder->reverse == 0 ? raw_count : -raw_count);

    // 3. 累计总数
    encoder->total_count += encoder->count;

    // 4. 计算原始速度 (cm/s)
    // 转动圈数 = 脉冲数 / 轮子转一圈的脉冲数
    // 行驶距离 = 转动圈数 * 车轮周长
    // 小车速度 = 行驶距离/ 采样时间
    float raw_speed_cm_s = (float)encoder->count / ENCODER_PPR * WHEEL_CIRCUMFERENCE_CM / SAMPLING_TIME_S;
    float raw_speed_rpm  = (float)encoder->count / ENCODER_PPR / SAMPLING_TIME_S * 60.0f;

    // 5. 一阶低通滤波 (减少抖动对PID的影响)
    encoder->speed_cm_s = (ENCODER_FILTER_ALPHA * raw_speed_cm_s) + ((1.0f - ENCODER_FILTER_ALPHA) * encoder->speed_cm_s);
    encoder->speed_rpm  = (ENCODER_FILTER_ALPHA * raw_speed_rpm)  + ((1.0f - ENCODER_FILTER_ALPHA) * encoder->speed_rpm);
}

float Encoder_GetSpeedRpm(Encoder_t *encoder)
{
    return encoder->speed_rpm;
}

float Encoder_GetSpeedLine(Encoder_t *encoder)
{
    return encoder->speed_cm_s;
}
