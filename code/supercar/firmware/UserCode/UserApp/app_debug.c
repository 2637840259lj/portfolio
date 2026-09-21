/**
 * @file      app_debug.c
 * @brief     编码器左转45° → 直行120cm(IMU防偏) → 编码器右转45°
 */
#include "app_debug.h"

#include "encoder_driver.h"
#include "pid.h"
#include "app_indicator.h"
#include "app_imu.h"
#include "app_position_hold.h"
#include "sys_time.h"
#include "at4950.h"
#include "led.h"
#include "buzzer.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>

extern Encoder_t encoderLeft, encoderRight;
extern MOTOR_t   motorLeft, motorRight;
extern PID_t     pidLeftSpeed, pidRightSpeed;
extern float     targetSpeedLeft, targetSpeedRight;

/* ==================== 调试开关 ==================== */
#define TURN_TEST_ONLY  1       /* 1=只测转弯精度, 0=左转+直行+右转组合 */

/* ==================== 参数 ==================== */
/* 转弯 (原地转) */
#define IPTURN_SPEED    20.0f    /* 原地转轮速 */
#define IPTURN_TOTAL    1400     /* 90° 实测校准 */

/* 直行 (IMU防偏) */
#define STRAIGHT_DIST   8330     /* 120cm */
#define STRAIGHT_MAX    80.0f    /* 最高速 */
#define YAW_KP          1.5f     /* 航向PID */
#define YAW_KD          0.2f
#define YAW_MAX         30.0f

/* ==================== 内部状态 ==================== */
static bool     s_running    = false;
static float    s_baseSpeed  = 0.0f;
static uint32_t s_startMs    = 0;
static uint8_t  s_phase      = 0;   /* 0=左转, 1=直行, 2=右转 */
static int32_t  s_encStartL  = 0;   /* 转弯起始编码器 */
static int32_t  s_encStartR  = 0;
static int32_t  s_straightEnc0 = 0;  /* 直行起始 */
static PID_t    pidYawHold;
static float    s_yawTarget  = 0.0f;

/* 位置保持锁止 */
static PosHold_t s_holdL, s_holdR;
static bool      s_hold_locked = false;
static uint32_t  s_hold_start  = 0;

extern bool g_poshold_active;  /* defined in empty.c */

/* ==================== 辅助 ==================== */
static void DoBrake(void)
{
    Motor_SetSpeed(&motorLeft,0); Motor_SetSpeed(&motorRight,0);
    Motor_Brake(&motorLeft); Motor_Brake(&motorRight);
    targetSpeedLeft=0; targetSpeedRight=0;
    PID_SetTarget(&pidLeftSpeed,0); PID_SetTarget(&pidRightSpeed,0);
    s_running=false;
    led_off(&led_green); led_on(&led_red);
}

/* 原地旋转: 左右轮反向, 检查总脉冲 */
static bool IPTurnStep(bool turn_left)
{
    int32_t dL = abs(encoderLeft.total_count - s_encStartL);
    int32_t dR = abs(encoderRight.total_count - s_encStartR);
    if(dL + dR >= IPTURN_TOTAL) return true;

    if(turn_left){
        targetSpeedLeft  = -IPTURN_SPEED;  /* 左轮倒转 */
        targetSpeedRight = +IPTURN_SPEED;  /* 右轮正转 */
    } else {
        targetSpeedLeft  = +IPTURN_SPEED;
        targetSpeedRight = -IPTURN_SPEED;
    }
    return false;
}

/* ==================== API ==================== */

void Debug_Init(void)
{
    s_running=false; s_baseSpeed=0; s_phase=0;
    PID_Init(&pidYawHold,POSITION_TYPE,YAW_KP,0,YAW_KD, 999,-999,YAW_MAX,-YAW_MAX);
    PID_SetTarget(&pidYawHold,0);
    PosHold_Init(&s_holdL, 1.5f, 0.3f, 3, 800);
    PosHold_Init(&s_holdR, 1.5f, 0.3f, 3, 800);
    printf("=== DEBUG: Turn 90deg Test ===\r\n");
}

void Debug_Start(void)
{
    if(s_running)return;
    s_running=true; s_baseSpeed=0; s_startMs=mspm0_get_clock_ms();
    s_hold_locked=false; g_poshold_active=false;
    s_phase=0; s_straightEnc0=0;
    s_encStartL=encoderLeft.total_count; s_encStartR=encoderRight.total_count;
    PID_Reset(&pidLeftSpeed); PID_Reset(&pidRightSpeed); PID_Reset(&pidYawHold);
    PID_SetTarget(&pidLeftSpeed,0); PID_SetTarget(&pidRightSpeed,0);
    /* 锁定直行航向 (转弯时不用, 但先锁住当前方向) */
    if(g_imuData.hw_ok){ s_yawTarget=g_imuData.yaw; PID_SetTarget(&pidYawHold,s_yawTarget); }
    led_on(&led_green); led_off(&led_red);
    buzzer_on(&buzzer0); mspm0_delay_ms(80); buzzer_off(&buzzer0);
    printf("=== START ===\r\n");
}

void Debug_Stop(void){DoBrake(); s_baseSpeed=0; printf("=== STOPPED ===\r\n");}
bool Debug_IsRunning(void){return s_running;}

/* ==================== 主调度 ==================== */

void Debug_Task(void)
{
    if(!s_running && !s_hold_locked) return;

    /* 锁止阶段: 位置保持500ms后刹车 */
    if(s_hold_locked){
        int16_t pl=PosHold_Update(&s_holdL, encoderLeft.total_count);
        int16_t pr=PosHold_Update(&s_holdR, encoderRight.total_count);
        Motor_SetSpeed(&motorLeft,pl); Motor_SetSpeed(&motorRight,pr);
        if(mspm0_get_clock_ms() - s_hold_start > 500){
            g_poshold_active = false; s_hold_locked = false;
            PosHold_Disable(&s_holdL); PosHold_Disable(&s_holdR);
            DoBrake();
            led_off(&led_green); led_on(&led_red);
            buzzer_on(&buzzer0); mspm0_delay_ms(200); buzzer_off(&buzzer0);
            printf("=== LOCKED & STOPPED ===\r\n");
            App_Indicator_RequestLongBeep();
        }
        return;
    }

    if(!s_running) return;
    uint32_t el = mspm0_get_clock_ms() - s_startMs;

#if TURN_TEST_ONLY
    /* ---- 转弯精度测试: 只做左转 ---- */
    if(IPTurnStep(true)){
        int32_t dL=abs(encoderLeft.total_count-s_encStartL);
        int32_t dR=abs(encoderRight.total_count-s_encStartR);
        /* 原地锁止500ms防惯性偏移 */
        g_poshold_active = true;
        PosHold_Enable(&s_holdL, encoderLeft.total_count);
        PosHold_Enable(&s_holdR, encoderRight.total_count);
        s_hold_locked = true; s_hold_start = mspm0_get_clock_ms();
        printf("=== TURN DONE, locking... ===\r\n");
    } else {
        static uint32_t tlp=0;
        if(el-(tlp-s_startMs)>=500||!tlp){tlp=el+s_startMs;
            int32_t dL=abs(encoderLeft.total_count-s_encStartL);
            int32_t dR=abs(encoderRight.total_count-s_encStartR);
            printf("[%4ums] L=%d R=%d sum=%d\r\n",(int)el,(int)dL,(int)dR,(int)(dL+dR));}
    }
#else
    /* ---- 0: 编码器左转45° ---- */
    if(s_phase == 0){
        if(IPTurnStep(true)){  /* true=左转 */
            s_phase = 1;
            s_straightEnc0 = encoderLeft.total_count;
            /* 重新锁航向: 直行起点的方向 */
            if(g_imuData.hw_ok){ s_yawTarget=g_imuData.yaw; PID_Reset(&pidYawHold); PID_SetTarget(&pidYawHold,s_yawTarget); }
            printf(">>> STRAIGHT (yaw=%.1f)\r\n", g_imuData.yaw);
        }
    }

    /* ---- 1: 直行120cm + IMU防偏 ---- */
    if(s_phase == 1){
        int32_t dist = abs(encoderLeft.total_count - s_straightEnc0);
        if(dist >= STRAIGHT_DIST && el > 500){
            s_phase = 2;
            s_encStartL=encoderLeft.total_count; s_encStartR=encoderRight.total_count;
            printf(">>> TURN R\r\n");
        } else {
            /* 速度 */
            if(s_baseSpeed < 10) s_baseSpeed = 10;
            s_baseSpeed += 1.5f;
            if(s_baseSpeed > STRAIGHT_MAX) s_baseSpeed = STRAIGHT_MAX;

            /* IMU航向修正 */
            if(g_imuData.hw_ok){
                float steer = PID_Calc(&pidYawHold, g_imuData.yaw);
                float sl = s_baseSpeed - steer;
                float sr = s_baseSpeed + steer;
                if(sl < 5) sl = 5; if(sr < 5) sr = 5;
                targetSpeedLeft = sl; targetSpeedRight = sr;
            } else {
                targetSpeedLeft = s_baseSpeed;
                targetSpeedRight = s_baseSpeed;
            }

            static uint32_t lp=0;
            if(el-(lp-s_startMs)>=1000||!lp){lp=el+s_startMs;
                printf("[ST %4ums] cur=%d spd=%.0f\r\n",(int)el,(int)dist,s_baseSpeed);}
        }
    }

    /* ---- 2: 编码器右转45° (左轮快) ---- */
    if(s_phase == 2){
        if(IPTurnStep(false)){  /* false=右转 */
            DoBrake();
            printf("=== DONE! time=%ums ===\r\n",(int)el);
            App_Indicator_RequestLongBeep();
            return;
        }
    }

    PID_SetTarget(&pidLeftSpeed, targetSpeedLeft);
    PID_SetTarget(&pidRightSpeed, targetSpeedRight);
#endif
}
