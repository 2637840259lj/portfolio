/**
 * @file    app_page.c
 * @brief   页面数据查询 + 硬件状态上报 — 实现
 */

#include "app_page.h"
#include "ble_protocol.h"
#include "ble_param.h"
#include "encoder_driver.h"
#include "app_imu.h"
#include <string.h>

/* 外部变量 */
extern Encoder_t encoderLeft;
extern Encoder_t encoderRight;
extern float     g_ble_target_left;
extern float     g_ble_target_right;
extern uint8_t   g_ble_state;

/* 外部函数 */
extern uint8_t Get_Gray_Digital(void);

/* 辅助序列化 */
static void w_i32(uint8_t *d, uint8_t off, int32_t v) { d[off]=v&0xFF; d[off+1]=(v>>8)&0xFF; d[off+2]=(v>>16)&0xFF; d[off+3]=(v>>24)&0xFF; }
static void w_i16(uint8_t *d, uint8_t off, int16_t v) { d[off]=v&0xFF; d[off+1]=(v>>8)&0xFF; }
static void w_f32(uint8_t *d, uint8_t off, float v) { uint32_t u; memcpy(&u, &v, 4); w_i32(d, off, (int32_t)u); }

/* ==================== GET_HW_STATUS ==================== */
void AppPage_HandleGetHWStatus(void)
{
    uint8_t buf[52];
    uint8_t off = 0;
    float spdL, spdR;
    uint8_t digital;

    /* ---- HW Status (8 bytes) ---- */
    buf[0]  = g_imuData.hw_ok ? 0x01 : 0x00;
    buf[0] |= 0x02;  /* ENC_OK */
    digital = Get_Gray_Digital();
    buf[0] |= (digital != 0xFF && digital != 0x00) ? 0x04 : 0x00; /* GRAY_OK */
    buf[0] |= 0x08;  /* MOTOR_OK */
    buf[1]  = g_imuData.hw_ok ? 0x68 : 0x00;
    buf[2]  = 0x03;
    buf[3]  = digital;
    buf[4]  = 0x03;
    buf[5]  = 0x00;
    buf[6]  = 0;
    buf[7]  = 0;
    off = 8;

    /* ---- Remote Page Data (附送, 省掉单独的 GET_PAGE_DATA 5字节帧) ---- */
    /* state(1) + encL(4) + encR(4) + yaw(4) + spdL(2) + spdR(2) + tgtL(2) + tgtR(2) + digital(1) = 22 */
    buf[off++] = g_ble_state;
    w_i32(buf, off, encoderLeft.total_count);  off += 4;
    w_i32(buf, off, encoderRight.total_count); off += 4;
    w_f32(buf, off, g_imuData.yaw);            off += 4;
    spdL = Encoder_GetSpeedLine(&encoderLeft);
    spdR = Encoder_GetSpeedLine(&encoderRight);
    w_i16(buf, off, (int16_t)(spdL * 10.0f));  off += 2;
    w_i16(buf, off, (int16_t)(spdR * 10.0f));  off += 2;
    w_i16(buf, off, (int16_t)(g_ble_target_left * 10.0f));  off += 2;
    w_i16(buf, off, (int16_t)(g_ble_target_right * 10.0f)); off += 2;
    buf[off++] = digital;

    BLE_Protocol_Send(BLE_CMD_GET_HW_STATUS, buf, off);
}

/* ==================== GET_PAGE_DATA ==================== */
void AppPage_HandleGetPageData(uint8_t page_id)
{
    uint8_t buf[72];
    uint8_t len = 0;

    switch (page_id) {

    case PAGE_REMOTE: /* 0x01 遥控/状态页 — 28 bytes */
        /* HW flags(1) + state(1) + encL(4) + encR(4) + yaw(4) + speedL(2) + speedR(2) + targetL(2) + targetR(2) + digital(1) + batt(1) + uptime(1) + err(1) = 26 */
        buf[0] = g_imuData.hw_ok ? 0x01 : 0x00;
        buf[0] |= 0x02; /* ENC assumed OK */
        uint8_t d2 = Get_Gray_Digital();
        buf[0] |= (d2 != 0xFF) ? 0x04 : 0x00;
        buf[0] |= 0x08; /* MOTOR assumed OK */
        buf[1]  = g_ble_state;
        w_i32(buf, 2,  encoderLeft.total_count);
        w_i32(buf, 6,  encoderRight.total_count);
        w_f32(buf, 10, g_imuData.yaw);
        float spdL = Encoder_GetSpeedLine(&encoderLeft);
        float spdR = Encoder_GetSpeedLine(&encoderRight);
        w_i16(buf, 14, (int16_t)(spdL * 10.0f));
        w_i16(buf, 16, (int16_t)(spdR * 10.0f));
        w_i16(buf, 18, (int16_t)(g_ble_target_left * 10.0f));
        w_i16(buf, 20, (int16_t)(g_ble_target_right * 10.0f));
        buf[22] = d2;
        buf[23] = 0;  /* battery */
        buf[24] = 0;  /* uptime */
        buf[25] = 0;  /* err */
        len = 26;
        break;

    case PAGE_MOTOR: /* 0x02 电机调试页 */
        /* encL(4) + encR(4) + spdL(4) + spdR(4) + tgtL(4) + tgtR(4) + kp(4) + ki(4) + kd(4) + kf(4) = 40 */
        w_i32(buf, 0,  encoderLeft.total_count);
        w_i32(buf, 4,  encoderRight.total_count);
        w_f32(buf, 8,  spdL);
        w_f32(buf, 12, spdR);
        w_f32(buf, 16, g_ble_target_left);
        w_f32(buf, 20, g_ble_target_right);
        w_f32(buf, 24, BLE_Param_Read(0x00)); /* KP */
        w_f32(buf, 28, BLE_Param_Read(0x01)); /* KI */
        w_f32(buf, 32, BLE_Param_Read(0x02)); /* KD */
        w_f32(buf, 36, BLE_Param_Read(0x03)); /* FF */
        len = 40;
        break;

    case PAGE_IMU: /* 0x03 陀螺仪页 */
        /* roll(4) + pitch(4) + yaw(4) + gx(4) + gy(4) + gz(4) + ax(4) + ay(4) + az(4) = 36 */
        w_f32(buf, 0,  g_imuData.roll);
        w_f32(buf, 4,  g_imuData.pitch);
        w_f32(buf, 8,  g_imuData.yaw);
        w_f32(buf, 12, g_imuData.gx);
        w_f32(buf, 16, g_imuData.gy);
        w_f32(buf, 20, g_imuData.gz);
        w_f32(buf, 24, g_imuData.ax);
        w_f32(buf, 28, g_imuData.ay);
        w_f32(buf, 32, g_imuData.az);
        len = 36;
        break;

    case PAGE_LINE: /* 0x04 循迹调试页 */
        /* digital(1) + reserved(1) + deviation_f32(4) + line_kp(4) + line_kd(4) = 14 */
        /* (灰度原始值需要读取8路ADC, 暂不实现, 后续扩展) */
        buf[0] = Get_Gray_Digital();
        buf[1] = 0;
        w_f32(buf, 2,  0.0f); /* deviation (暂未接入循迹 PID) */
        w_f32(buf, 6,  BLE_Param_Read(0x04)); /* LINE_KP */
        w_f32(buf, 10, BLE_Param_Read(0x05)); /* LINE_KD */
        len = 14;
        break;

    case PAGE_TURN: { /* 0x05 转弯调试页 */
        /* inner(4) + outer(4) + target(4) + diff(4) = 16 */
        int32_t diff = encoderLeft.total_count - encoderRight.total_count;
        w_f32(buf, 0,  BLE_Param_Read(0x09)); /* TURN_INNER */
        w_f32(buf, 4,  BLE_Param_Read(0x0A)); /* TURN_OUTER */
        w_f32(buf, 8,  BLE_Param_Read(0x0B)); /* TURN_TARGET */
        w_i32(buf, 12, diff);
        len = 16;
        break;
    }

    case PAGE_PARAMS: /* 0x06 参数总览页 */
        /* 17 个 float = 68 bytes */
        {
            uint8_t i;
            for (i = 0; i < g_param_count && i < 17; i++) {
                uint8_t pid;
                for (pid = 0; pid < g_param_count; pid++) {
                    /* 遍历参数表找到 id==i 的条目 */
                    /* 简化: 直接按 g_param_table 顺序打包 */
                }
            }
            /* 按参数表顺序直接打包 */
            extern param_entry_t g_param_table[];
            for (i = 0; i < g_param_count; i++) {
                w_f32(buf, i * 4, *(g_param_table[i].ptr));
            }
            len = g_param_count * 4;
        }
        break;

    default:
        len = 0;
        break;
    }

    if (len > 0) {
        BLE_Protocol_Send(BLE_CMD_GET_PAGE_DATA, buf, len);
    }
}
