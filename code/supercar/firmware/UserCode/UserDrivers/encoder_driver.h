#ifndef __ENCODER_DRIVER_H__
#define __ENCODER_DRIVER_H__

#include "stdint.h"

#define ENCODER_LINES_4X (13.0f * 4) // 编码器线数 （4倍频后）
#define GEAR_RATIO 28.0f             // 减速比
// 轮子转一圈的脉冲数 (PPR)
#define ENCODER_PPR (ENCODER_LINES_4X * GEAR_RATIO)

// 车轮直径 (单位: 厘米)
#define WHEEL_DIAMETER_CM 6.5f
// 车轮周长
#define PI 3.14159265f
#define WHEEL_CIRCUMFERENCE_CM (WHEEL_DIAMETER_CM * PI)

#define SAMPLING_TIME_S 0.01f // 采样时间
#define ENCODER_FILTER_ALPHA 0.9f // 一阶低通滤波系数 (0.0~1.0, 越小滤波越强/越平滑，但响应延迟也越大)

typedef struct
{
    volatile int32_t temp_count;
    int32_t count;
    int32_t total_count; // 累计总计数值
    uint8_t reverse;     // 编码器的方向是否反转。0-正常，1-反转
    float speed_rpm;
    float speed_cm_s;
    uint8_t prev_state;  // 保存上一次的引脚状态

} Encoder_t;

extern const int8_t Encoder_QuadTable[16];

void Encoder_Driver_Init(Encoder_t *encoder, unsigned char reverse);

void Encoder_Update(Encoder_t *encoder);

float Encoder_GetSpeedRpm(Encoder_t *encoder);
float Encoder_GetSpeedLine(Encoder_t *encoder);



#endif