/**
 * @file      app_key.c
 * @brief     按键处理 (ebtn 库)
 */
#include "app_key.h"
#include "ebtn.h"
#include "sys_time.h"
#include "app_competition.h"
#include "app_stepper_test.h"
#include <stdio.h>

/* ==================== 按键参数 ==================== */
#define KEY_ID_1  1
#define KEY_ID_2  2
#define KEY_ID_3  3

/* 消抖20ms, 单击50~500ms, 双击间隔450ms, 长按周期1000ms, 最多5连击 */
static ebtn_btn_param_t kBtnParam = EBTN_PARAMS_INIT(20, 20, 50, 500, 450, 1000, 5);

static ebtn_btn_t btns[3] = {
    EBTN_BUTTON_INIT(KEY_ID_1, &kBtnParam),
    EBTN_BUTTON_INIT(KEY_ID_2, &kBtnParam),
    EBTN_BUTTON_INIT(KEY_ID_3, &kBtnParam),
};

/* ==================== GPIO 读取 ==================== */
static uint8_t get_btn_state(ebtn_btn_t *btn)
{
    switch (btn->key_id) {
    case KEY_ID_1: return DL_GPIO_readPins(GPIO_KEY_PORT, GPIO_KEY_PIN_1_PIN) ? 0 : 1;
    case KEY_ID_2: return DL_GPIO_readPins(GPIO_KEY_PORT, GPIO_KEY_PIN_2_PIN) ? 0 : 1;
    case KEY_ID_3: return DL_GPIO_readPins(GPIO_KEY_PORT, GPIO_KEY_PIN_3_PIN) ? 0 : 1;
    }
    return 1;
}

/* ==================== 事件回调 ==================== */
static void btn_evt(ebtn_btn_t *btn, ebtn_evt_t evt)
{
#if STEPPER_TEST_MODE_ENABLE
    /* Test mode owns all three keys. K3 acts at debounced press, not delayed click. */
    if (StepperTest_IsExclusive()) {
        if ((btn->key_id == KEY_ID_3) && (evt == EBTN_EVT_ONPRESS)) {
            StepperTest_OnKey3Press();
        } else if (evt == EBTN_EVT_ONCLICK) {
            if (btn->key_id == KEY_ID_1) {
                StepperTest_OnKey1();
            } else if (btn->key_id == KEY_ID_2) {
                StepperTest_OnKey2();
            }
        }
        return;
    }
#endif

    if (evt == EBTN_EVT_ONCLICK) {
        /* H题底盘任务操作：KEY1上一任务，KEY3下一任务，KEY2启动/运行中安全停止。 */
        if (btn->key_id == KEY_ID_1) {
            if (Competition_GetState() != COMP_STATE_RUNNING) Competition_PrevTask();
        } else if (btn->key_id == KEY_ID_3) {
            if (Competition_GetState() != COMP_STATE_RUNNING) Competition_NextTask();
        } else if (btn->key_id == KEY_ID_2) {
            if (Competition_GetState() == COMP_STATE_RUNNING) Competition_Stop();
            else Competition_Start();
        }
    }
}

/* ==================== API ==================== */
int APP_Key_Init(void)
{
    ebtn_init(btns, 3, NULL, 0, get_btn_state, btn_evt);
    printf("Keys: 3 buttons ready\r\n");
    return 1;
}

void APP_Key_Task(void)
{
    ebtn_process((ebtn_time_t)mspm0_get_clock_ms());
}
