#ifndef NO_MCU_GANV_GRAYSCALE_SENSOR_CONFIG_H_
#define NO_MCU_GANV_GRAYSCALE_SENSOR_CONFIG_H_

#include <string.h>
#include "ti_msp_dl_config.h"


/* 传感器版本定义 */
#define Class		    0

/* ADC分辨率位宽定义 */
#define _14Bits 0
#define _12Bits 1
#define _10Bits 2
#define _8Bits  3

/* 当前传感器版本 */
#define Sensor_Edition Class

/* 传感器方向配置: 1为正向, 0为反向 */
#define Direction 1
/* 当前使用的ADC位数（12位） */
#define Sensor_ADCbits _12Bits

/* 多路复用器地址引脚控制宏：通过3个引脚选择8个通道 */
#define Switch_Address_0(i) ((i)?(DL_GPIO_setPins(GPIO_GRAY_PORT,GPIO_GRAY_AD0_PIN)) : (DL_GPIO_clearPins(GPIO_GRAY_PORT,GPIO_GRAY_AD0_PIN)))
#define Switch_Address_1(i) ((i)?(DL_GPIO_setPins(GPIO_GRAY_PORT,GPIO_GRAY_AD1_PIN)) : (DL_GPIO_clearPins(GPIO_GRAY_PORT,GPIO_GRAY_AD1_PIN)))
#define Switch_Address_2(i) ((i)?(DL_GPIO_setPins(GPIO_GRAY_PORT,GPIO_GRAY_AD2_PIN)) : (DL_GPIO_clearPins(GPIO_GRAY_PORT,GPIO_GRAY_AD2_PIN)))

/* 获取ADC值的用户宏接口 */
#define Get_adc_of_user() adc_getValue()

/**
 * @brief 灰度传感器数据与状态结构体
 * 包含原始数据、校准数据、阈值、归一化数据及运行状态
 */
typedef struct {
    unsigned short Analog_value[8];     // 8个通道的原始模拟值
    unsigned short Normal_value[8];     // 8个通道的归一化后的值
    unsigned short Calibrated_white[8]; // 标定的白色参考基准值
    unsigned short Calibrated_black[8]; // 标定的黑色参考基准值
    unsigned short Gray_white[8];       // 判定为白色的动态阈值
    unsigned short Gray_black[8];       // 判定为黑色的动态阈值
	double Normal_factor[8];            // 归一化比例系数

	double bits;                        // ADC最大值上限(根据设定的位数决定)
    unsigned char Digtal;               // 8位数字量结果(每一位代表一个探头的黑白状态)
    unsigned char Time_out;             // 超时设定时间
    unsigned char Tick;                 // 计时器计数
    unsigned char ok;                   // 初始化完成标志(1:就绪, 0:未就绪)
} No_MCU_Sensor;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 传感器初步初始化，清零所有缓存和状态变量
 * @param sensor 传感器结构体指针
 */
void No_MCU_Ganv_Sensor_Init_Frist(No_MCU_Sensor* sensor);

/**
 * @brief 传感器完整初始化，计算黑白阈值和归一化系数
 * @param sensor 传感器结构体指针
 * @param Calibrated_white 标定好的白色基准值数组指针(8个元素)
 * @param Calibrated_black 标定好的黑色基准值数组指针(8个元素)
 */
void No_MCU_Ganv_Sensor_Init(No_MCU_Sensor* sensor,unsigned short* Calibrated_white, unsigned short* Calibrated_black);

/**
 * @brief 传感器常规处理任务(无滴答定时器依赖)
 * 依次执行: 采集模拟量 -> 转换为数字量 -> 归一化处理
 * @param sensor 传感器结构体指针
 */
void No_Mcu_Ganv_Sensor_Task_Without_tick(No_MCU_Sensor* sensor);

/**
 * @brief 获取传感器的数字量(0-255，对应8个探头的黑白状态)
 * @param sensor 传感器结构体指针
 * @return 8位数字量状态
 */
unsigned char Get_Digtal_For_User(No_MCU_Sensor* sensor);

/**
 * @brief 获取归一化后的数据数组
 * @param sensor 传感器结构体指针
 * @param result 用于存放结果的数组指针
 * @return 是否获取成功 (1:成功, 0:未初始化完成)
 */
unsigned char Get_Normalize_For_User(No_MCU_Sensor* sensor,unsigned short* result);

/**
 * @brief 获取原始模拟量数据数组
 * @param sensor 传感器结构体指针
 * @param result 用于存放结果的数组指针
 * @return 是否获取成功 (1:成功, 0:未初始化完成)
 */
unsigned char Get_Anolog_Value(No_MCU_Sensor* sensor,unsigned short* result);

/**
 * @brief 计算加权归一化值，常用于巡线误差（质心）计算
 * @param sensor 传感器结构体指针
 * @param field 场地背景类型(0:白底黑线, 1:黑底白线)
 * @return float 计算出的偏差位置值。偏离中心越大绝对值越大
 */
float CalculateNormalizedValue(No_MCU_Sensor *sensor, uint8_t field);

/**
 * @brief 计算数字量加权偏差值（基础版循迹算法）
 * @param Digital 8位数字量状态
 * @param field 场地背景类型(0:白底黑线, 1:黑底白线)
 * @return int32_t 计算出的加权偏差位置。如果完全脱线，返回上一次的脱线记忆。
 */
int32_t CalculateDigitalWeightedValue(unsigned char Digital, uint8_t field);

#ifdef __cplusplus
}
#endif

#endif /* NO_MCU_GANV_GRAYSCALE_SENSOR_CONFIG_H_ */
