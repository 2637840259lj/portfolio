#ifndef MPU6050_H_
#define MPU6050_H_

#include <stdint.h>

#define MPU_ADDR 0x68

#define MPU_SAMPLE_RATE_REG 0x19
#define MPU_CFG_REG 0x1A
#define MPU_GYRO_CFG_REG 0x1B
#define MPU_ACCEL_CFG_REG 0x1C
#define MPU_FIFO_EN_REG 0x23
#define MPU_INTBP_CFG_REG 0x37
#define MPU_INT_EN_REG 0x38
#define MPU_ACCEL_XOUTH_REG 0x3B
#define MPU_TEMP_OUTH_REG 0x41
#define MPU_GYRO_XOUTH_REG 0x43
#define MPU_USER_CTRL_REG 0x6A
#define MPU_PWR_MGMT1_REG 0x6B
#define MPU_PWR_MGMT2_REG 0x6C
#define MPU_DEVICE_ID_REG 0x75

uint8_t MPU_Init(void);
uint8_t MPU_Set_Gyro_Fsr(uint8_t fsr);
uint8_t MPU_Set_Accel_Fsr(uint8_t fsr);
uint8_t MPU_Set_LPF(uint16_t lpf);
uint8_t MPU_Set_Rate(uint16_t rate);
short MPU_Get_Temperature(void);
uint8_t MPU_Get_Gyroscope(short *gx, short *gy, short *gz);
uint8_t MPU_Get_Gyroscope_Calibrated(short *gx, short *gy, short *gz);
uint8_t MPU_Get_Accelerometer(short *ax, short *ay, short *az);
uint8_t MPU_Get_Accelerometer_Calibrated(short *ax, short *ay, short *az);
uint8_t MPU_Calibrate_Gyro(uint16_t samples);

/* 将非阻塞校准任务得到的原始均值一次性写入零偏。 */
void MPU_SetCalibrationBias(int32_t gx, int32_t gy, int32_t gz,
                            int32_t ax, int32_t ay, int32_t az);

/* Z 轴陀螺仪零偏：单位为原始 LSB，供上位机静止漂移闭环做小步修正。 */
int32_t MPU_GetGyroBiasZ(void);
void    MPU_AdjustGyroBiasZ(int16_t delta_lsb);

uint8_t MPU_Write_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf);
uint8_t MPU_Read_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf);
uint8_t MPU_Write_Byte(uint8_t reg, uint8_t data);
uint8_t MPU_Read_Byte(uint8_t reg);

#endif
