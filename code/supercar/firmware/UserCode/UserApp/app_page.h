#ifndef __APP_PAGE_H
#define __APP_PAGE_H

/**
 * @file    app_page.h
 * @brief   页面数据查询 + 硬件状态上报
 *
 * GET_PAGE_DATA(page_id) → 返回该页面所需数据 (单包, <=72 字节)
 * GET_HW_STATUS         → 返回 8 字节硬件状态位图
 */

#include <stdint.h>
#include <stdbool.h>

/* page_id 定义 (与 App 约定一致) */
#define PAGE_REMOTE      0x01
#define PAGE_MOTOR       0x02
#define PAGE_IMU         0x03
#define PAGE_LINE        0x04
#define PAGE_TURN        0x05
#define PAGE_PARAMS      0x06

void AppPage_HandleGetHWStatus(void);
void AppPage_HandleGetPageData(uint8_t page_id);

#endif /* __APP_PAGE_H */
