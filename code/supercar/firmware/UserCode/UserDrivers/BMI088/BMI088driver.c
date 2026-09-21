#include "BMI088driver.h"
#include "BMI088reg.h"
#include "BMI088Middleware.h"
#include <stdio.h>

float BMI088_ACCEL_SEN = BMI088_ACCEL_3G_SEN;
float BMI088_GYRO_SEN = BMI088_GYRO_2000_SEN;

#if defined(BMI088_USE_SPI)
/**
 * @brief BMI088加速度计SPI写单个寄存器
 * @param reg       寄存器地址
 * @param data      写入的数据
 */
#define BMI088_accel_write_single_reg(reg, data)                                                                       \
    {                                                                                                                  \
        BMI088_ACCEL_NS_L();                                                                                           \
        BMI088_delay_us(1);                                                                                            \
        BMI088_write_single_reg((reg), (data));                                                                        \
        BMI088_delay_us(1);                                                                                            \
        BMI088_ACCEL_NS_H();                                                                                           \
        BMI088_delay_us(1);                                                                                            \
    }
/**
************************************************************************
* @brief:      	BMI088_accel_read_single_reg(reg, data)
* @param:       reg - 寄存器地址
*               data - 读出的数据
* @retval:     	void
* @details:    	BMI088加速度计SPI读单个寄存器
************************************************************************
**/
#define BMI088_accel_read_single_reg(reg, data)                                                                        \
    {                                                                                                                  \
        BMI088_ACCEL_NS_L();                                                                                           \
        BMI088_delay_us(1);                                                                                            \
        BMI088_read_write_byte((reg) | 0x80);                                                                          \
        BMI088_read_write_byte(0x55);                                                                                  \
        (data) = BMI088_read_write_byte(0x55);                                                                         \
        BMI088_delay_us(1);                                                                                            \
        BMI088_ACCEL_NS_H();                                                                                           \
        BMI088_delay_us(1);                                                                                            \
    }
/**
************************************************************************
* @brief:      	BMI088_accel_read_muli_reg(reg, data, len)
* @param:       reg - 起始寄存器地址
*               data - 存储数据的缓冲区
*               len - 读取长度
* @retval:     	void
* @details:    	BMI088加速度计SPI读多个寄存器
************************************************************************
**/
#define BMI088_accel_read_muli_reg(reg, data, len)                                                                     \
    {                                                                                                                  \
        BMI088_ACCEL_NS_L();                                                                                           \
        BMI088_delay_us(1);                                                                                            \
        BMI088_read_write_byte((reg) | 0x80);                                                                          \
        BMI088_read_write_byte(0x55);                                                                                  \
        BMI088_read_muli_reg_body(data, len);                                                                          \
        BMI088_delay_us(1);                                                                                            \
        BMI088_ACCEL_NS_H();                                                                                           \
        BMI088_delay_us(1);                                                                                            \
    }
/**
 * @brief BMI088陀螺仪SPI写单个寄存器
 * @param reg       寄存器地址
 * @param data      写入的数据
 */
#define BMI088_gyro_write_single_reg(reg, data)                                                                        \
    {                                                                                                                  \
        BMI088_GYRO_NS_L();                                                                                            \
        BMI088_delay_us(1);                                                                                            \
        BMI088_write_single_reg((reg), (data));                                                                        \
        BMI088_delay_us(1);                                                                                            \
        BMI088_GYRO_NS_H();                                                                                            \
        BMI088_delay_us(1);                                                                                            \
    }
/**
 * @brief BMI088陀螺仪SPI读单个寄存器
 * @param reg       寄存器地址
 * @param data      读出的数据
 */
#define BMI088_gyro_read_single_reg(reg, data)                                                                         \
    {                                                                                                                  \
        BMI088_GYRO_NS_L();                                                                                            \
        BMI088_delay_us(1);                                                                                            \
        BMI088_read_single_reg((reg), &(data));                                                                        \
        BMI088_delay_us(1);                                                                                            \
        BMI088_GYRO_NS_H();                                                                                            \
        BMI088_delay_us(1);                                                                                            \
    }
/**
 * @brief BMI088陀螺仪SPI读多个寄存器
 * @param reg       起始寄存器地址
 * @param data      存储数据的缓冲区
 * @param len       读取长度
 */
#define BMI088_gyro_read_muli_reg(reg, data, len)                                                                      \
    {                                                                                                                  \
        BMI088_GYRO_NS_L();                                                                                            \
        BMI088_delay_us(1);                                                                                            \
        BMI088_read_write_byte((reg) | 0x80);                                                                          \
        BMI088_read_muli_reg_body((data), (len));                                                                      \
        BMI088_delay_us(1);                                                                                            \
        BMI088_GYRO_NS_H();                                                                                            \
        BMI088_delay_us(1);                                                                                            \
    }

static void BMI088_write_single_reg(uint8_t reg, uint8_t data);
static void BMI088_read_single_reg(uint8_t reg, uint8_t *return_data);
// static void BMI088_write_muli_reg(uint8_t reg, uint8_t* buf, uint8_t len );
static void BMI088_read_muli_reg(uint8_t reg, uint8_t *buf, uint8_t len);
static void BMI088_read_muli_reg_body(uint8_t *buf, uint8_t len);

#elif defined(BMI088_USE_IIC)

#endif
/**
 * @brief 初始化BMI088加速度计寄存器配置数据及错误代码
 */
static uint8_t write_BMI088_accel_reg_data_error[BMI088_WRITE_ACCEL_REG_NUM][3] = {
    {BMI088_ACC_PWR_CTRL, BMI088_ACC_ENABLE_ACC_ON, BMI088_ACC_PWR_CTRL_ERROR},
    {BMI088_ACC_PWR_CONF, BMI088_ACC_PWR_ACTIVE_MODE, BMI088_ACC_PWR_CONF_ERROR},
    {BMI088_ACC_CONF, BMI088_ACC_NORMAL | BMI088_ACC_800_HZ | BMI088_ACC_CONF_MUST_Set, BMI088_ACC_CONF_ERROR},
    {BMI088_ACC_RANGE, BMI088_ACC_RANGE_3G, BMI088_ACC_RANGE_ERROR},
    {BMI088_INT1_IO_CTRL,
        BMI088_ACC_INT1_IO_ENABLE | BMI088_ACC_INT1_GPIO_PP | BMI088_ACC_INT1_GPIO_LOW,
        BMI088_INT1_IO_CTRL_ERROR},
    {BMI088_INT_MAP_DATA, BMI088_ACC_INT1_DRDY_INTERRUPT, BMI088_INT_MAP_DATA_ERROR}

};
/**
 * @brief 初始化BMI088陀螺仪寄存器配置数据及错误代码
 * */
static uint8_t write_BMI088_gyro_reg_data_error[BMI088_WRITE_GYRO_REG_NUM][3] = {
    {BMI088_GYRO_RANGE, BMI088_GYRO_2000, BMI088_GYRO_RANGE_ERROR},
    {BMI088_GYRO_BANDWIDTH, BMI088_GYRO_1000_116_HZ | BMI088_GYRO_BANDWIDTH_MUST_Set, BMI088_GYRO_BANDWIDTH_ERROR},
    {BMI088_GYRO_LPM1, BMI088_GYRO_NORMAL_MODE, BMI088_GYRO_LPM1_ERROR},
    {BMI088_GYRO_CTRL, BMI088_DRDY_ON, BMI088_GYRO_CTRL_ERROR},
    {BMI088_GYRO_INT3_INT4_IO_CONF,
        BMI088_GYRO_INT3_GPIO_PP | BMI088_GYRO_INT3_GPIO_LOW,
        BMI088_GYRO_INT3_INT4_IO_CONF_ERROR},
    {BMI088_GYRO_INT3_INT4_IO_MAP, BMI088_GYRO_DRDY_IO_INT3, BMI088_GYRO_INT3_INT4_IO_MAP_ERROR}

};
/**
 * @brief BMI088初始化函数，包含GPIO、SPI通信以及传感器的初始化
 * @return       错误代码
 * */
uint8_t BMI088_init(void)
{
    uint8_t error = BMI088_NO_ERROR;
    // GPIO and SPI  Init .
    BMI088_GPIO_init();
    BMI088_com_init();

    error |= bmi088_accel_init();
    error |= bmi088_gyro_init();

    return error;
}

/**
 * @brief 测试 SPI 通讯是否正常
 * @return 0: 正常, 1: 异常
 */
uint8_t BMI088_test_spi_communication(void)
{
    uint8_t accel_id = 0;
    uint8_t gyro_id = 0;

    // 唤醒并测试加速度计SPI
    // 加速度计在SPI模式下第一次读取可能是无效的（用于切换到SPI模式）
    BMI088_accel_read_single_reg(BMI088_ACC_CHIP_ID, accel_id);
    BMI088_delay_us(150);
    BMI088_accel_read_single_reg(BMI088_ACC_CHIP_ID, accel_id);

    // 测试陀螺仪SPI
    BMI088_gyro_read_single_reg(BMI088_GYRO_CHIP_ID, gyro_id);

    printf("SPI Test -> Accel ID: 0x%02X (Expected: 0x1E), Gyro ID: 0x%02X (Expected: 0x0F or 0x22)\r\n",
        accel_id,
        gyro_id);

    if (accel_id == BMI088_ACC_CHIP_ID_VALUE && (gyro_id == BMI088_GYRO_CHIP_ID_VALUE || gyro_id == 0x22))
    {
        return 0; // 正常
    }

    return 1; // 异常
}

/**
 * @brief BMI088加速度计初始化，进行软复位并写入寄存器配置
 * @return       错误代码
 * */
uint8_t bmi088_accel_init(void)
{
    uint8_t res = 0;
    uint8_t write_reg_num = 0;

    // check commiunication
    BMI088_accel_read_single_reg(BMI088_ACC_CHIP_ID, res);
    BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);
    BMI088_accel_read_single_reg(BMI088_ACC_CHIP_ID, res);
    BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);

    // accel software reset
    BMI088_accel_write_single_reg(BMI088_ACC_SOFTRESET, BMI088_ACC_SOFTRESET_VALUE);
    BMI088_delay_ms(BMI088_LONG_DELAY_TIME);

    // check commiunication is normal after reset
    BMI088_accel_read_single_reg(BMI088_ACC_CHIP_ID, res);
    BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);
    BMI088_accel_read_single_reg(BMI088_ACC_CHIP_ID, res);
    BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);

    // check the "who am I"
    if (res != BMI088_ACC_CHIP_ID_VALUE)
    {
        return BMI088_NO_SENSOR;
    }

    // set accel sonsor config and check
    for (write_reg_num = 0; write_reg_num < BMI088_WRITE_ACCEL_REG_NUM; write_reg_num++)
    {

        BMI088_accel_write_single_reg(
            write_BMI088_accel_reg_data_error[write_reg_num][0], write_BMI088_accel_reg_data_error[write_reg_num][1]);
        BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);

        BMI088_accel_read_single_reg(write_BMI088_accel_reg_data_error[write_reg_num][0], res);
        BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);

        if (res != write_BMI088_accel_reg_data_error[write_reg_num][1])
        {
            return write_BMI088_accel_reg_data_error[write_reg_num][2];
        }
    }
    return BMI088_NO_ERROR;
}
/**
 * @brief BMI088陀螺仪初始化，进行软复位并写入寄存器配置
 * @return       错误代码
 */
uint8_t bmi088_gyro_init(void)
{
    uint8_t write_reg_num = 0;
    uint8_t res = 0;

    // check communication
    BMI088_gyro_read_single_reg(BMI088_GYRO_CHIP_ID, res);
    BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);
    BMI088_gyro_read_single_reg(BMI088_GYRO_CHIP_ID, res);
    BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);

    // reset the gyro sensor
    BMI088_gyro_write_single_reg(BMI088_GYRO_SOFTRESET, BMI088_GYRO_SOFTRESET_VALUE);
    BMI088_delay_ms(BMI088_LONG_DELAY_TIME);
    // check communication is normal after reset
    BMI088_gyro_read_single_reg(BMI088_GYRO_CHIP_ID, res);
    BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);
    BMI088_gyro_read_single_reg(BMI088_GYRO_CHIP_ID, res);
    BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);

    // check the "who am I"
    if (res != BMI088_GYRO_CHIP_ID_VALUE && res != 0x22)
    {
        return BMI088_NO_SENSOR;
    }

    // set gyro sonsor config and check
    for (write_reg_num = 0; write_reg_num < BMI088_WRITE_GYRO_REG_NUM; write_reg_num++)
    {

        BMI088_gyro_write_single_reg(
            write_BMI088_gyro_reg_data_error[write_reg_num][0], write_BMI088_gyro_reg_data_error[write_reg_num][1]);
        BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);

        BMI088_gyro_read_single_reg(write_BMI088_gyro_reg_data_error[write_reg_num][0], res);
        BMI088_delay_us(BMI088_COM_WAIT_SENSOR_TIME);

        if (res != write_BMI088_gyro_reg_data_error[write_reg_num][1])
        {
            return write_BMI088_gyro_reg_data_error[write_reg_num][2];
        }
    }

    return BMI088_NO_ERROR;
}
/**
 * @brief 读取BMI088的陀螺仪、加速度计和温度传感器数据
 * @param gyro      陀螺仪数据 (x, y, z)
 * @param accel     加速度数据 (x, y, z)
 * @param temperate 温度数据
 */
void BMI088_read(float gyro[3], float accel[3], float *temperate)
{
    uint8_t buf[8] = {0, 0, 0, 0, 0, 0};
    int16_t bmi088_raw_temp;

    BMI088_accel_read_muli_reg(BMI088_ACCEL_XOUT_L, buf, 6);

    bmi088_raw_temp = (int16_t)((buf[1]) << 8) | buf[0];
    accel[0] = bmi088_raw_temp * BMI088_ACCEL_SEN;
    bmi088_raw_temp = (int16_t)((buf[3]) << 8) | buf[2];
    accel[1] = bmi088_raw_temp * BMI088_ACCEL_SEN;
    bmi088_raw_temp = (int16_t)((buf[5]) << 8) | buf[4];
    accel[2] = bmi088_raw_temp * BMI088_ACCEL_SEN;

    BMI088_gyro_read_muli_reg(BMI088_GYRO_CHIP_ID, buf, 8);
    if (buf[0] == BMI088_GYRO_CHIP_ID_VALUE || buf[0] == 0x22)
    {
        bmi088_raw_temp = (int16_t)((buf[3]) << 8) | buf[2];
        gyro[0] = bmi088_raw_temp * BMI088_GYRO_SEN;
        bmi088_raw_temp = (int16_t)((buf[5]) << 8) | buf[4];
        gyro[1] = bmi088_raw_temp * BMI088_GYRO_SEN;
        bmi088_raw_temp = (int16_t)((buf[7]) << 8) | buf[6];
        gyro[2] = bmi088_raw_temp * BMI088_GYRO_SEN;
    }
    BMI088_accel_read_muli_reg(BMI088_TEMP_M, buf, 2);

    bmi088_raw_temp = (int16_t)((buf[0] << 3) | (buf[1] >> 5));

    if (bmi088_raw_temp > 1023)
    {
        bmi088_raw_temp -= 2048;
    }

    *temperate = bmi088_raw_temp * BMI088_TEMP_FACTOR + BMI088_TEMP_OFFSET;
}

#if defined(BMI088_USE_SPI)
/**
 * @brief 向BMI088写入单个寄存器
 * @param reg       寄存器地址
 * @param data      写入的数据
 */
static void BMI088_write_single_reg(uint8_t reg, uint8_t data)
{
    BMI088_read_write_byte(reg);
    BMI088_read_write_byte(data);
}
/**
 * @brief 从BMI088读取单个寄存器
 * @param reg       寄存器地址
 * @param return_data 返回的数据
 */
static void BMI088_read_single_reg(uint8_t reg, uint8_t *return_data)
{
    BMI088_read_write_byte(reg | 0x80);
    *return_data = BMI088_read_write_byte(0x55);
}

// static void BMI088_write_muli_reg(uint8_t reg, uint8_t* buf, uint8_t len )
//{
//     BMI088_read_write_byte( reg );
//     while( len != 0 )
//     {

//        BMI088_read_write_byte( *buf );
//        buf ++;
//        len --;
//    }

//}
/**
 * @brief 从BMI088读取多个寄存器
 * @param reg       起始寄存器地址
 * @param buf       存储数据的缓冲区
 * @param len       读取长度
 */
static void BMI088_read_muli_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    BMI088_read_write_byte(reg | 0x80);

    while (len != 0)
    {

        *buf = BMI088_read_write_byte(0x55);
        buf++;
        len--;
    }
}

static void BMI088_read_muli_reg_body(uint8_t *buf, uint8_t len)
{
    while (len != 0)
    {
        *buf = BMI088_read_write_byte(0x55);
        buf++;
        len--;
    }
}
#elif defined(BMI088_USE_IIC)

#endif
