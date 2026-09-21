
#include "pid.h"
#include <math.h>


/**
 * @brief PID结构体初始化函数
 * @param pid               PID结构体指针
 * @param pid_type          PID类型
 * @param kp,ki,kd          PID三个参数
 * @param sum_error_limit_p 积分限幅上限
 * @param sum_error_limit_n 积分限幅下限
 * @param output_limit_p    输出限幅上限
 * @param output_limit_n    输出限幅下限
 * */
void PID_Init(PID_t* pid,
                           const PID_Type_t pid_type, 
                           const float kp, 
                           const float ki, 
                           const float kd,
                           const float sum_error_limit_p, 
                           const float sum_error_limit_n,
                           const float output_limit_p, 
                           const float output_limit_n)
{
    pid->pid_type = pid_type;
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->kf = 0.0f; // 默认前馈系数为0
    
    pid->sum_error_limit_p = sum_error_limit_p;
    pid->sum_error_limit_n = sum_error_limit_n;
    pid->output_limit_p = output_limit_p;
    pid->output_limit_n = output_limit_n;
    
    /* 初始化中间变量 */
    pid->target = 0.0f;
    pid->error = 0.0f;
    pid->pre_error = 0.0f;
    pid->sum_error = 0.0f;
    pid->output = 0.0f;
}

/**
 * @brief PID结构体初始化函数(简化版，不设置限幅)
 * @param pid               PID结构体指针
 * @param pid_type          PID类型
 * @param kp,ki,kd          PID三个参数
 * */
void PID_InitSimple(PID_t* pid,
                                const PID_Type_t pid_type,
                                const float kp,
                                const float ki,
                                const float kd)
{
    PID_Init(pid, pid_type, kp, ki, kd, FLT_MAX, -FLT_MAX, FLT_MAX, -FLT_MAX);
}

/**
 * @brief 设置PID目标值
 * @param pid       PID结构体指针
 * @param target    新的PID目标值
 * */
void PID_SetTarget(PID_t* pid, const float target)
{
    pid->target = target;
}

/**
 * @brief 设置前馈系数
 * @param pid       PID结构体指针
 * @param kf        前馈系数
 * */
void PID_SetFeedforward(PID_t* pid, const float kf)
{
    pid->kf = kf;
}

/**
 * @brief 设置积分值,用于高自由度的积分限幅
 * @param pid           PID结构体指针
 * @param sum_error     PID积分值
 */
void PID_SetSumError(PID_t* pid, const float sum_error)
{
    pid->sum_error = sum_error;
}

/**
 * @brief PID计算
 * @param pid       PID结构体指针
 * @param input     PID观测值
 * @return          PID计算结果
 * */
float PID_Calc(PID_t* pid, const float input)
{
    const float error_ = pid->target - input;
    
    if (pid->pid_type == POSITION_TYPE) {
        /***位置式PID公式:u=Kpe(t)+Ki*e(t)的积分+Kd[e(t)-e(t-1)]***/
        pid->sum_error += error_;
        
        /*积分限幅*/
        if (pid->sum_error_limit_p < FLT_MAX)
            if (pid->sum_error >= pid->sum_error_limit_p) pid->sum_error = pid->sum_error_limit_p;
        if (pid->sum_error_limit_n > -FLT_MAX)
            if (pid->sum_error <= pid->sum_error_limit_n) pid->sum_error = pid->sum_error_limit_n;
            
        pid->output = pid->kp * error_ +
                      pid->ki * pid->sum_error +
                      pid->kd * (error_ - pid->error) +
                      pid->kf * pid->target;
        pid->error = error_;
    } else if (pid->pid_type == DELTA_TYPE) {
        /***增量式PID公式:du=Kp[e(t)-e(t-1)]+Kie(t)+Kd[e(t)-2e(t-1)+e(t-2)]***/
        pid->output += pid->kp * (error_ - pid->error) +
                       pid->ki * (error_) +
                       pid->kd * (error_ - 2 * pid->error + pid->pre_error);
        pid->pre_error = pid->error;
        pid->error = error_;
    }
    
    /*输出限幅*/
    if (pid->output_limit_p < FLT_MAX)
        if (pid->output >= pid->output_limit_p) pid->output = pid->output_limit_p;
    if (pid->output_limit_n > -FLT_MAX)
        if (pid->output <= pid->output_limit_n) pid->output = pid->output_limit_n;

    return pid->output;
}

/**
 * @brief 重置PID内部状态
 * @param pid       PID结构体指针
 * */
void PID_Reset(PID_t* pid)
{
    pid->error = 0.0f;
    pid->pre_error = 0.0f;
    pid->sum_error = 0.0f;
    pid->output = 0.0f;
}

/**
 * @brief 获取当前PID输出值
 * @param pid       PID结构体指针
 * @return          当前PID输出值
 * */
float PID_GetOutput(const PID_t* pid)
{
    return pid->output;
}

/**
 * @brief 获取当前误差值
 * @param pid           PID结构体指针
 * @param current_value 当前测量值
 * @return              当前误差值
 * */
float PID_GetCurrentError(const PID_t* pid, const float current_value)
{
    return pid->target - current_value;
}