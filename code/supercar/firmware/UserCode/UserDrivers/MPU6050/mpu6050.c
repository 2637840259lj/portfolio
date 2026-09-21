#include "mpu6050.h"
#include "sys_time.h"
#include "ti_msp_dl_config.h"
#include <stdint.h>
#include <stdio.h>

#define I2C_TIMEOUT_MS 10
/* 外设忙态不受 RX_DONE/TX_DONE 超时保护；若 I2C 总线停在 Busy，
 * 旧代码会在启动任何传输前无限自旋，从而让 CAL 卡住 MCU 主循环、
 * 蓝牙 UART 也无法再被消费。运行期超时仅中止本次传输并向上层返回失败。 */
#define I2C_IDLE_TIMEOUT_MS 10
/* 时基异常或异常上下文中 SysTick 未推进时，毫秒超时条件不会成立。
 * 使用独立的轮询上限，确保任何 I2C 等待最多只占用约一个短控制周期。 */
#define I2C_SPIN_LIMIT 1000000UL

/* 由H题App“静止60秒采样”确认后回填；重新烧录后即作为上电初始零偏。 */
/* H题硬编码静止零偏：2026-07-30 App 60秒/6000样本均值。 */
int32_t s_gyroBias[3] = {1,0,29};
int32_t s_accelBias[3] = {740,270,-184};
static uint8_t s_gyroBiasReady;

/* ---- I2C SDA 总线解锁（仅允许在上电初始化阶段使用） ---- */
static void mpu_i2c_sda_unlock(void)
{
    uint8_t cycleCnt = 0;
    DL_I2C_reset(I2C_0_INST);
    DL_GPIO_initDigitalOutput(GPIO_I2C_0_IOMUX_SCL);
    DL_GPIO_initDigitalInputFeatures(GPIO_I2C_0_IOMUX_SDA,
                                     DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
                                     DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_clearPins(GPIO_I2C_0_SCL_PORT, GPIO_I2C_0_SCL_PIN);
    DL_GPIO_enableOutput(GPIO_I2C_0_SCL_PORT, GPIO_I2C_0_SCL_PIN);

    do
    {
        DL_GPIO_clearPins(GPIO_I2C_0_SCL_PORT, GPIO_I2C_0_SCL_PIN);
        mspm0_delay_ms(1);
        DL_GPIO_setPins(GPIO_I2C_0_SCL_PORT, GPIO_I2C_0_SCL_PIN);
        mspm0_delay_ms(1);

        if (DL_GPIO_readPins(GPIO_I2C_0_SDA_PORT, GPIO_I2C_0_SDA_PIN))
            break;
    } while (++cycleCnt < 9);  /* 最多约18ms，通信故障时不得长时间阻塞主循环 */

    DL_I2C_reset(I2C_0_INST);
    DL_GPIO_initPeripheralInputFunctionFeatures(GPIO_I2C_0_IOMUX_SDA,
                                                GPIO_I2C_0_IOMUX_SDA_FUNC,
                                                DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
                                                DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_initPeripheralInputFunctionFeatures(GPIO_I2C_0_IOMUX_SCL,
                                                GPIO_I2C_0_IOMUX_SCL_FUNC,
                                                DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_NONE,
                                                DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_enableHiZ(GPIO_I2C_0_IOMUX_SDA);
    DL_GPIO_enableHiZ(GPIO_I2C_0_IOMUX_SCL);
    DL_I2C_enablePower(I2C_0_INST);
    SYSCFG_DL_I2C_0_init();
}

/* 运行期传输失败只复位 I2C 控制器，不再把 SDA/SCL 动态改成 GPIO 并执行延时脉冲。
 * CAL 时该恢复动作发生在主循环；若底层外设状态异常，旧的 GPIO 重配可能与 SysTick/
 * UART 中断并发，导致整个 MCU 失去响应。总线物理被拉低时本次读取失败即可，由上层
 * 快速结束校准并回 NACK；下一次上电初始化才允许完整 SDA 解锁。 */
static void mpu_i2c_abort_transfer(void)
{
    I2C_0_INST->MASTER.MCTR = 0U;
    DL_I2C_resetControllerTransfer(I2C_0_INST);
    DL_I2C_flushControllerTXFIFO(I2C_0_INST);
}

/* ---- I2C 写多字节 ---- */
static uint8_t mpu_i2c_write(uint8_t slave_addr, uint8_t reg_addr, uint8_t length, uint8_t const *data)
{
    uint32_t cnt = length;
    uint8_t const *ptr = data;
    uint32_t start = 0;
    uint32_t cur = 0;
    uint32_t spins = 0;

    if (!length)
        return 0;

    start = mspm0_get_clock_ms();

    DL_I2C_transmitControllerData(I2C_0_INST, reg_addr);
    DL_I2C_clearInterruptStatus(I2C_0_INST, DL_I2C_INTERRUPT_CONTROLLER_TX_DONE);

    while (!(DL_I2C_getControllerStatus(I2C_0_INST) & DL_I2C_CONTROLLER_STATUS_IDLE)) {
        if ((mspm0_get_clock_ms() - start) >= I2C_IDLE_TIMEOUT_MS || ++spins >= I2C_SPIN_LIMIT) {
            mpu_i2c_abort_transfer();
            return 1;
        }
    }

    DL_I2C_startControllerTransfer(I2C_0_INST, slave_addr, DL_I2C_CONTROLLER_DIRECTION_TX, (uint32_t)length + 1U);

    do
    {
        uint32_t fillcnt = DL_I2C_fillControllerTXFIFO(I2C_0_INST, (uint8_t *)ptr, cnt);
        cnt -= fillcnt;
        ptr += fillcnt;

        cur = mspm0_get_clock_ms();
        if ((cur - start) >= I2C_TIMEOUT_MS || ++spins >= I2C_SPIN_LIMIT)
        {
            mpu_i2c_abort_transfer();
            return 1;
        }
    } while (!DL_I2C_getRawInterruptStatus(I2C_0_INST, DL_I2C_INTERRUPT_CONTROLLER_TX_DONE));

    return 0;
}

/* ---- I2C 读多字节 ---- */
static uint8_t mpu_i2c_read(uint8_t slave_addr, uint8_t reg_addr, uint8_t length, uint8_t *data)
{
    uint32_t i = 0;
    uint32_t start = 0;
    uint32_t cur = 0;
    uint32_t spins = 0;

    if (!length)
        return 0;

    start = mspm0_get_clock_ms();

    DL_I2C_transmitControllerData(I2C_0_INST, reg_addr);
    I2C_0_INST->MASTER.MCTR = I2C_MCTR_RD_ON_TXEMPTY_ENABLE;
    DL_I2C_clearInterruptStatus(I2C_0_INST, DL_I2C_INTERRUPT_CONTROLLER_RX_DONE);

    while (!(DL_I2C_getControllerStatus(I2C_0_INST) & DL_I2C_CONTROLLER_STATUS_IDLE)) {
        if ((mspm0_get_clock_ms() - start) >= I2C_IDLE_TIMEOUT_MS || ++spins >= I2C_SPIN_LIMIT) {
            mpu_i2c_abort_transfer();
            return 1;
        }
    }

    DL_I2C_startControllerTransfer(I2C_0_INST, slave_addr, DL_I2C_CONTROLLER_DIRECTION_RX, length);

    do
    {
        if (!DL_I2C_isControllerRXFIFOEmpty(I2C_0_INST))
        {
            uint8_t c = DL_I2C_receiveControllerData(I2C_0_INST);
            if (i < length)
            {
                data[i] = c;
                ++i;
            }
        }

        cur = mspm0_get_clock_ms();
        if ((cur - start) >= I2C_TIMEOUT_MS || ++spins >= I2C_SPIN_LIMIT)
        {
            mpu_i2c_abort_transfer();
            return 1;
        }
    } while (!DL_I2C_getRawInterruptStatus(I2C_0_INST, DL_I2C_INTERRUPT_CONTROLLER_RX_DONE));

    if (!DL_I2C_isControllerRXFIFOEmpty(I2C_0_INST))
    {
        uint8_t c = DL_I2C_receiveControllerData(I2C_0_INST);
        if (i < length)
        {
            data[i] = c;
            ++i;
        }
    }

    I2C_0_INST->MASTER.MCTR = 0;
    DL_I2C_flushControllerTXFIFO(I2C_0_INST);

    return (i == length) ? 0 : 1;
}

/* ==================== MPU6050 初始化 ==================== */

uint8_t MPU_Init(void)
{
    uint8_t res;

    if (DL_I2C_getSDAStatus(I2C_0_INST) == DL_I2C_CONTROLLER_SDA_LOW)
    {
        printf("MPU: SDA low, unlocking...\r\n");
        mpu_i2c_sda_unlock();
    }

    MPU_Write_Byte(MPU_PWR_MGMT1_REG, 0x80);
    mspm0_delay_ms(100);
    MPU_Write_Byte(MPU_PWR_MGMT1_REG, 0x00);
    MPU_Set_Gyro_Fsr(3);
    MPU_Set_Accel_Fsr(0);
    MPU_Set_Rate(50);
    MPU_Write_Byte(MPU_INT_EN_REG, 0x00);
    MPU_Write_Byte(MPU_USER_CTRL_REG, 0x00);
    MPU_Write_Byte(MPU_FIFO_EN_REG, 0x00);
    MPU_Write_Byte(MPU_INTBP_CFG_REG, 0x80);

    res = MPU_Read_Byte(MPU_DEVICE_ID_REG);
    printf("MPU: WHO_AM_I=0x%02X (expect 0x%02X)\r\n", res, MPU_ADDR);

    if (res == MPU_ADDR)
    {
        MPU_Write_Byte(MPU_PWR_MGMT1_REG, 0x01);
        MPU_Write_Byte(MPU_PWR_MGMT2_REG, 0x00);
        MPU_Set_Rate(100);
        return 0;
    }

    /* 地址 0x68 失败, 尝试 0x69 (AD0=高电平) */
    printf("MPU: trying addr 0x69...\r\n");
    {
        uint8_t buf[1] = {0};
        /* 临时用 0x69 读 WHO_AM_I */
        DL_I2C_transmitControllerData(I2C_0_INST, MPU_DEVICE_ID_REG);
        I2C_0_INST->MASTER.MCTR = I2C_MCTR_RD_ON_TXEMPTY_ENABLE;
        DL_I2C_clearInterruptStatus(I2C_0_INST, DL_I2C_INTERRUPT_CONTROLLER_RX_DONE);
        {
            uint32_t idle_start = mspm0_get_clock_ms();
            while (!(DL_I2C_getControllerStatus(I2C_0_INST) & DL_I2C_CONTROLLER_STATUS_IDLE)) {
                if ((mspm0_get_clock_ms() - idle_start) >= I2C_IDLE_TIMEOUT_MS) {
                    mpu_i2c_abort_transfer();
                    return 1;
                }
            }
        }
        DL_I2C_startControllerTransfer(I2C_0_INST, 0x69, DL_I2C_CONTROLLER_DIRECTION_RX, 1);
        uint32_t t0 = mspm0_get_clock_ms();
        do {
            if (!DL_I2C_isControllerRXFIFOEmpty(I2C_0_INST))
            {
                buf[0] = DL_I2C_receiveControllerData(I2C_0_INST);
                break;
            }
        } while ((mspm0_get_clock_ms() - t0) < I2C_TIMEOUT_MS);
        I2C_0_INST->MASTER.MCTR = 0;
        DL_I2C_flushControllerTXFIFO(I2C_0_INST);
        printf("MPU: 0x69 WHO_AM_I=0x%02X\r\n", buf[0]);
    }

    return 1;
}

/* ==================== 陀螺仪+加速度计零偏校准 ==================== */

uint8_t MPU_Calibrate_Gyro(uint16_t samples)
{
    int32_t sumGX = 0, sumGY = 0, sumGZ = 0;
    int32_t sumAX = 0, sumAY = 0, sumAZ = 0;
    uint16_t count = 0;
    short gx = 0, gy = 0, gz = 0;
    short ax = 0, ay = 0, az = 0;

    if (samples == 0)
    {
        s_gyroBiasReady = 1;
        return 0;
    }

    for (uint16_t i = 0; i < samples; ++i)
    {
        if (MPU_Get_Gyroscope(&gx, &gy, &gz) == 0 &&
            MPU_Get_Accelerometer(&ax, &ay, &az) == 0)
        {
            sumGX += gx; sumGY += gy; sumGZ += gz;
            sumAX += ax; sumAY += ay; sumAZ += az;
            ++count;
        }
        mspm0_delay_ms(5);
    }

    /* 单次 I2C 偶发失败不应让校准看似成功；有效样本不足说明链路不稳定。 */
    if (count < (uint16_t)((samples * 9U) / 10U)) {
        printf("Gyro Cal failed: valid=%u/%u\r\n", count, samples);
        return 1;
    }

    s_gyroBias[0] = sumGX / (int32_t)count;
    s_gyroBias[1] = sumGY / (int32_t)count;
    s_gyroBias[2] = sumGZ / (int32_t)count;

    /* 加速度计 Z 轴减去 1g (16384 LSB, ±2g量程) 得到真实零偏 */
    s_accelBias[0] = sumAX / (int32_t)count;
    s_accelBias[1] = sumAY / (int32_t)count;
    s_accelBias[2] = sumAZ / (int32_t)count - 16384;

    s_gyroBiasReady = 1;

    printf("Gyro Bias:  X=%ld Y=%ld Z=%ld\r\n",
           (long)s_gyroBias[0], (long)s_gyroBias[1], (long)s_gyroBias[2]);
    printf("Accel Bias: X=%ld Y=%ld Z=%ld\r\n",
           (long)s_accelBias[0], (long)s_accelBias[1], (long)s_accelBias[2]);

    return 0;
}

void MPU_SetCalibrationBias(int32_t gx, int32_t gy, int32_t gz,
                            int32_t ax, int32_t ay, int32_t az)
{
    s_gyroBias[0] = gx;
    s_gyroBias[1] = gy;
    s_gyroBias[2] = gz;
    s_accelBias[0] = ax;
    s_accelBias[1] = ay;
    s_accelBias[2] = az - 16384;
    s_gyroBiasReady = 1;
    printf("Gyro Bias:  X=%ld Y=%ld Z=%ld\r\n", (long)gx, (long)gy, (long)gz);
}

int32_t MPU_GetGyroBiasZ(void)
{
    return s_gyroBias[2];
}

void MPU_AdjustGyroBiasZ(int16_t delta_lsb)
{
    /* 单次限幅，防止异常无线数据造成标定突变。±8 LSB ≈ ±0.49°/s。 */
    if (delta_lsb > 8) delta_lsb = 8;
    if (delta_lsb < -8) delta_lsb = -8;
    s_gyroBias[2] += delta_lsb;
    printf("Gyro Bias Z adjust=%d -> %ld\r\n", delta_lsb, (long)s_gyroBias[2]);
}

/* ==================== 配置函数 ==================== */

uint8_t MPU_Set_Gyro_Fsr(uint8_t fsr)
{
    return MPU_Write_Byte(MPU_GYRO_CFG_REG, fsr << 3);
}

uint8_t MPU_Set_Accel_Fsr(uint8_t fsr)
{
    return MPU_Write_Byte(MPU_ACCEL_CFG_REG, fsr << 3);
}

uint8_t MPU_Set_LPF(uint16_t lpf)
{
    uint8_t data = 0;
    if (lpf >= 188)
        data = 1;
    else if (lpf >= 98)
        data = 2;
    else if (lpf >= 42)
        data = 3;
    else if (lpf >= 20)
        data = 4;
    else if (lpf >= 10)
        data = 5;
    else
        data = 6;
    return MPU_Write_Byte(MPU_CFG_REG, data);
}

uint8_t MPU_Set_Rate(uint16_t rate)
{
    uint8_t data;
    if (rate > 1000)
        rate = 1000;
    if (rate < 4)
        rate = 4;
    data = 1000 / rate - 1;
    MPU_Write_Byte(MPU_SAMPLE_RATE_REG, data);
    return MPU_Set_LPF(rate / 2);
}

/* ==================== 数据读取 ==================== */

short MPU_Get_Temperature(void)
{
    uint8_t buf[2];
    short raw;
    float temp;
    MPU_Read_Len(MPU_ADDR, MPU_TEMP_OUTH_REG, 2, buf);
    raw = ((uint16_t)buf[0] << 8) | buf[1];
    temp = 36.53f + ((float)raw) / 340.0f;
    return (short)(temp * 100.0f);
}

uint8_t MPU_Get_Gyroscope(short *gx, short *gy, short *gz)
{
    uint8_t buf[6];
    uint8_t res = MPU_Read_Len(MPU_ADDR, MPU_GYRO_XOUTH_REG, 6, buf);
    if (res == 0)
    {
        *gx = (short)(((uint16_t)buf[0] << 8) | buf[1]);
        *gy = (short)(((uint16_t)buf[2] << 8) | buf[3]);
        *gz = (short)(((uint16_t)buf[4] << 8) | buf[5]);
    }
    return res;
}

uint8_t MPU_Get_Gyroscope_Calibrated(short *gx, short *gy, short *gz)
{
    uint8_t res = MPU_Get_Gyroscope(gx, gy, gz);
    if (res == 0 && s_gyroBiasReady)
    {
        *gx = (short)(*gx - (short)s_gyroBias[0]);
        *gy = (short)(*gy - (short)s_gyroBias[1]);
        *gz = (short)(*gz - (short)s_gyroBias[2]);
    }
    return res;
}

uint8_t MPU_Get_Accelerometer(short *ax, short *ay, short *az)
{
    uint8_t buf[6];
    uint8_t res = MPU_Read_Len(MPU_ADDR, MPU_ACCEL_XOUTH_REG, 6, buf);
    if (res == 0)
    {
        *ax = (short)(((uint16_t)buf[0] << 8) | buf[1]);
        *ay = (short)(((uint16_t)buf[2] << 8) | buf[3]);
        *az = (short)(((uint16_t)buf[4] << 8) | buf[5]);
    }
    return res;
}

uint8_t MPU_Get_Accelerometer_Calibrated(short *ax, short *ay, short *az)
{
    uint8_t res = MPU_Get_Accelerometer(ax, ay, az);
    if (res == 0 && s_gyroBiasReady)
    {
        *ax = (short)(*ax - (short)s_accelBias[0]);
        *ay = (short)(*ay - (short)s_accelBias[1]);
        *az = (short)(*az - (short)s_accelBias[2]);
    }
    return res;
}

/* ==================== 底层 I2C 封装 ==================== */

uint8_t MPU_Write_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    return mpu_i2c_write(addr, reg, len, buf);
}

uint8_t MPU_Read_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    return mpu_i2c_read(addr, reg, len, buf);
}

uint8_t MPU_Write_Byte(uint8_t reg, uint8_t data)
{
    return mpu_i2c_write(MPU_ADDR, reg, 1, &data);
}

uint8_t MPU_Read_Byte(uint8_t reg)
{
    uint8_t data = 0;
    if (mpu_i2c_read(MPU_ADDR, reg, 1, &data) != 0)
        return 0;
    return data;
}
