/**
 * @brief       PID基础控制库(C)
 * @details
 * @author
 * @date        2026-1-17
 * */

#ifndef _PID_H
#define _PID_H

#include <stdbool.h>
#include <float.h>

/* PID类型枚举 */
typedef enum {
    POSITION_TYPE,    // 位置式PID
    DELTA_TYPE        // 增量式PID
} PID_Type_t;

/* PID结构体定义 */
typedef struct {
    /* PID参数 */
    PID_Type_t pid_type;    // PID类型
    float kp;               // 比例系数
    float ki;               // 积分系数
    float kd;               // 微分系数
    float kf;               // 前馈系数
    
    /* 限幅参数 */
    float sum_error_limit_p;    // 积分限幅上限
    float sum_error_limit_n;    // 积分限幅下限
    float output_limit_p;       // 输出限幅上限
    float output_limit_n;       // 输出限幅下限
    
    /* 中间(运行时)变量 */
    float target;           // 目标值
    float error;            // 上一次的偏差值
    float pre_error;        // 上上一次的偏差值,仅增量式PID使用
    float sum_error;        // 累计偏差值,仅位置式PID使用
    float output;           // PID输出值
} PID_t;


void PID_Init(PID_t* pid,
                           const PID_Type_t pid_type, 
                           const float kp, 
                           const float ki, 
                           const float kd,
                           const float sum_error_limit_p, 
                           const float sum_error_limit_n,
                           const float output_limit_p, 
                           const float output_limit_n);
                           
void PID_InitSimple(PID_t* pid,
                                const PID_Type_t pid_type,
                                const float kp,
                                const float ki,
                                const float kd);
                                
void PID_SetTarget(PID_t* pid, const float target);

void PID_SetFeedforward(PID_t* pid, const float kf);

void PID_SetSumError(PID_t* pid, const float sum_error);

float PID_Calc(PID_t* pid, const float input);

void PID_Reset(PID_t* pid);

float PID_GetOutput(const PID_t* pid);

float PID_GetCurrentError(const PID_t* pid, const float current_value);


#endif // PID_C_H