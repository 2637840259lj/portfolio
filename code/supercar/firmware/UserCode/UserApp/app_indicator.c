/**
 * @file      app_indicator.c
 * @brief     LED + 蜂鸣器指示器实现 (竞赛版)
 */
#include "app_indicator.h"
#include "sys_time.h"
#include "led.h"
#include "buzzer.h"

typedef struct {
    bool     req_once;
    bool     req_long;       /* 长鸣请求 */
    uint8_t  beep_count;
} BuzzerRequest_t;

static BuzzerRequest_t s_req = {0};

static bool     s_mission_running = false;
static bool     s_red_blink_enable = false;
static bool     s_red_led_on = false;

static uint8_t  s_remaining_beeps = 0U;
static bool     s_buzzer_is_on = false;
static bool     s_buzzer_wait_gap = false;
static bool     s_long_beep_active = false;
static uint32_t s_buzzer_tick_ms = 0U;
static uint32_t s_long_beep_start_ms = 0U;

static uint32_t s_last_indicator_ms = 0U;
static uint32_t s_last_red_blink_ms = 0U;

/* 任务显示: 用颜色替代闪烁次数 */
static uint8_t  s_selected_task = 1;

void App_Indicator_SetSelectedTask(uint8_t task_id)
{
    if (task_id >= 1 && task_id <= 4)
        s_selected_task = task_id;
}

void App_IndicatorTask(void)
{
    uint32_t now = mspm0_get_clock_ms();

    /* ---- 蜂鸣器 ---- */
    if (s_req.req_long) {
        s_req.req_long = false;
        s_long_beep_active = true;
        s_long_beep_start_ms = now;
        buzzer_on(&buzzer0);
    }

    if (s_long_beep_active && ((now - s_long_beep_start_ms) >= 1000U)) {
        s_long_beep_active = false;
        buzzer_off(&buzzer0);
    }

    if (!s_long_beep_active) {
        if (s_req.beep_count > 0U) {
            s_remaining_beeps = s_req.beep_count;
            s_req.beep_count = 0U;
        }
        if (s_req.req_once) {
            s_req.req_once = false;
            if (s_remaining_beeps == 0U) s_remaining_beeps = 1U;
        }
        if ((s_remaining_beeps > 0U) && !s_buzzer_is_on && !s_buzzer_wait_gap) {
            s_buzzer_is_on = true;
            s_buzzer_tick_ms = now;
            buzzer_on(&buzzer0);
        }
        if (s_buzzer_is_on && ((now - s_buzzer_tick_ms) >= 80U)) {
            s_buzzer_is_on = false;
            s_buzzer_wait_gap = true;
            s_buzzer_tick_ms = now;
            buzzer_off(&buzzer0);
        }
        if (s_buzzer_wait_gap && ((now - s_buzzer_tick_ms) >= 80U)) {
            s_buzzer_wait_gap = false;
            if (s_remaining_beeps > 0U) s_remaining_beeps--;
        }
    }

    /* ---- LED (50ms刷新) ---- */
    if ((now - s_last_indicator_ms) < 50U) return;
    s_last_indicator_ms = now;

    /* 红灯闪烁 (任务失败) */
    if (s_red_blink_enable) {
        led_off(&led_green);
        led_off(&led_blue);
        if ((now - s_last_red_blink_ms) >= 250U) {
            s_last_red_blink_ms = now;
            s_red_led_on = !s_red_led_on;
            if (s_red_led_on) led_on(&led_red);
            else              led_off(&led_red);
        }
        return;
    }

    /* 正常运行/空闲 */
    s_red_led_on = false;
    if (s_mission_running) {
        /* 任务运行: 绿灯 */
        led_on(&led_green);
        led_off(&led_red);
        led_off(&led_blue);
    } else {
        /* 空闲: 按任务显示颜色 */
        led_off(&led_red);
        led_off(&led_green);
        led_off(&led_blue);
        switch (s_selected_task) {
        case 1: led_on(&led_red);                    break;  /* 任务1: 红 */
        case 2: led_on(&led_green);                  break;  /* 任务2: 绿 */
        case 3: led_on(&led_blue);                   break;  /* 任务3: 蓝 */
        case 4: led_on(&led_red); led_on(&led_blue); break;  /* 任务4: 紫 */
        default: break;
        }
    }
}

/* ---- API ---- */

void App_Indicator_RequestBeepOnce(void)
{
    s_req.req_once = true;
}

void App_Indicator_RequestBeepCount(uint8_t count)
{
    s_req.beep_count = count;
}

void App_Indicator_RequestLongBeep(void)
{
    s_req.req_long = true;
}

void App_Indicator_SetMissionRunning(bool is_running)
{
    s_mission_running = is_running;
}

void App_Indicator_SetRedBlink(bool enable)
{
    s_red_blink_enable = enable;
    if (!enable) {
        s_red_led_on = false;
        led_off(&led_red);
    }
}

void App_Indicator_ShowTask(uint8_t task_id)
{
    App_Indicator_SetSelectedTask(task_id);
}

void App_Indicator_PowerOnSelfTest(void)
{
    led_on(&led_red);   mspm0_delay_ms(150); led_off(&led_red);
    led_on(&led_green); mspm0_delay_ms(150); led_off(&led_green);
    led_on(&led_blue);  mspm0_delay_ms(150); led_off(&led_blue);
    buzzer_on(&buzzer0);  mspm0_delay_ms(150); buzzer_off(&buzzer0);
}

void App_Indicator_ImuResult(bool ok)
{
    if (ok) {
        led_on(&led_blue);
        buzzer_on(&buzzer0); mspm0_delay_ms(100); buzzer_off(&buzzer0);
        mspm0_delay_ms(400);
        led_off(&led_blue);
    } else {
        for (int i = 0; i < 3; i++) {
            led_on(&led_red);
            buzzer_on(&buzzer0); mspm0_delay_ms(100); buzzer_off(&buzzer0);
            led_off(&led_red);
            mspm0_delay_ms(100);
        }
    }
}
