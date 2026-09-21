#include "BMI088Middleware.h"
#include "ti_msp_dl_config.h"
#include "sys_time.h"

/**
 * @brief BMI088传感器GPIO初始化函数
 */
void BMI088_GPIO_init(void)
{
    // GPIO 已经在 SYSCFG 中初始化
}

/**
 * @brief BMI088传感器通信初始化函数
 */
void BMI088_com_init(void)
{
    // SPI 已经在 SYSCFG 中初始化
    // 清空可能存在的RX FIFO残留数据
    while (!DL_SPI_isRXFIFOEmpty(SPI_0_INST)) {
        DL_SPI_receiveData8(SPI_0_INST);
    }
}

/**
 * @brief 延迟指定毫秒数的函数
 * @param ms 要延迟的毫秒数
 */
void BMI088_delay_ms(uint16_t ms)
{
    mspm0_delay_ms(ms);
}

/**
 * @brief 微秒级延迟函数
 * @param us 要延迟的微秒数
 */
void BMI088_delay_us(uint16_t us)
{
    delay_us(us);
}

/**
 * @brief 将BMI088加速度计片选信号置低，使其处于选中状态
 */
void BMI088_ACCEL_NS_L(void)
{
    DL_GPIO_clearPins(GPIO_SPI_PORT, GPIO_SPI_ACC_CS_PIN);
}

/**
 * @brief 将BMI088加速度计片选信号置高，使其处于非选中状态
 */
void BMI088_ACCEL_NS_H(void)
{
    DL_GPIO_setPins(GPIO_SPI_PORT, GPIO_SPI_ACC_CS_PIN);
}

/**
 * @brief 将BMI088陀螺仪片选信号置低，使其处于选中状态
 */
void BMI088_GYRO_NS_L(void)
{
    DL_GPIO_clearPins(GPIO_SPI_PORT, GPIO_SPI_GYRO_CS_PIN);
}

/**
 * @brief 将BMI088陀螺仪片选信号置高，使其处于非选中状态
 */
void BMI088_GYRO_NS_H(void)
{
    DL_GPIO_setPins(GPIO_SPI_PORT, GPIO_SPI_GYRO_CS_PIN);
}

/**
 * @brief 通过BMI088使用的SPI总线进行单字节的读写操作
 * @param txdata 要发送的数据
 * @return       接收到的数据
 */
uint8_t BMI088_read_write_byte(uint8_t txdata)
{
    // 等待TX FIFO未满
    while(DL_SPI_isTXFIFOFull(SPI_0_INST));
    
    // 发送数据
    DL_SPI_transmitData8(SPI_0_INST, txdata);
    
    // 等待发送完成（这保证了时钟已全部发出，且 RX FIFO 肯定已经收到了对应的返回数据）
    while(DL_SPI_isBusy(SPI_0_INST));
    
    // 接收数据（直接读取，不使用 isRXFIFOEmpty 轮询，防止因为 FIFO 阈值配置问题死锁）
    return DL_SPI_receiveData8(SPI_0_INST);
}

