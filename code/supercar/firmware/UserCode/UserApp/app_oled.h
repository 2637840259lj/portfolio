#ifndef __APP_OLED_H
#define __APP_OLED_H

/**
 * @file    app_oled.h
 * @brief   0.96 寸 OLED SSD1306 硬件 I2C0 驱动
 *
 * @details
 *   使用硬件 I2C0 (PA0=SDA, PA1=SCL), 与 MPU6050 共享总线。
 *   MPU6050 地址 0x68, SSD1306 地址 0x3C, 不冲突。
 *   插驱动板自带 OLED 接口 (CN6/CN7), 无需飞线。
 *
 *   刷屏策略: 直接写 OLED, 每次 ShowString/ShowNum 立即生效。
 *   I2C0 为 100kHz, 刷全屏约 92ms, 建议仅在需要更新时调用。
 *   与 MPU6050 分时复用 (单线程, 不会并发访问 I2C0)。
 */

#include <stdint.h>

/* ==================== API ==================== */

/** 初始化 GPIO + SSD1306。在 main 中调用一次。 */
void AppOLED_Init(void);

/** 清屏。 */
void AppOLED_Clear(void);

/**
 * 显示字符串。
 * @param x     列 (0-127)
 * @param y     页 (0-7, 每页 8 像素高)
 * @param str   字符串
 * @param size  字号: 8 (6x8 小字) 或 16 (8x16 大字)
 */
void AppOLED_ShowString(uint8_t x, uint8_t y, const char *str, uint8_t size);

/**
 * 显示无符号整数。
 * @param x     列
 * @param y     页
 * @param num   数字
 * @param len   显示位数 (不足前导空格)
 * @param size  字号: 8 或 16
 */
void AppOLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size);

/**
 * 显示计时 (mm:ss.s 格式)。
 * @param ms  毫秒数
 * 在屏幕中央大字显示, 用于比赛计时。
 */
void AppOLED_ShowTime(uint32_t ms);

/**
 * 在指定位置显示计时 (mm:ss.s)。
 */
void AppOLED_ShowTimeAt(uint8_t x, uint8_t y, uint32_t ms, uint8_t size);

#endif /* __APP_OLED_H */
