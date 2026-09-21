/**
 * @file      app_key.h
 * @brief     按键处理 (ebtn 消抖)
 */
#ifndef APP_KEY_H
#define APP_KEY_H

#include <stdint.h>
#include <stdbool.h>
#include "ti_msp_dl_config.h"

int  APP_Key_Init(void);
void APP_Key_Task(void);

#endif /* APP_KEY_H */
