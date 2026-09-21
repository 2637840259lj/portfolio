#include "No_Mcu_Ganv_Grayscale_Sensor.h"
#include "bsp_adc.h"
#include "sys_time.h"

/**
 * @brief 获取所有8个通道的原始模拟值
 * 通过多路复用器切换地址引脚，依次读取ADC值并进行多次采样求平均
 * @param result 用于存储8个通道模拟值的数组
 */
void Get_Analog_value(unsigned short *result)
{
    unsigned char i, j;
    unsigned int Anolag = 0;

    for (i = 0; i < 8; i++)
    {
        // 通过3个GPIO引脚控制8选1多路复用器选择当前通道
        Switch_Address_0(!(i & 0x01));
        Switch_Address_1(!(i & 0x02));
        Switch_Address_2(!(i & 0x04));
        delay_us(1);
        // 每个通道连续采样8次以求和
        for (j = 0; j < 8; j++)
        {
            Anolag += Get_adc_of_user();
        }
        
        // 取平均值，并根据传感器的安装方向（Direction）决定存储顺序
        if (!Direction)
            result[i] = Anolag / 8;
        else
            result[7 - i] = Anolag / 8; // 反向安装时，倒序存储
            
        Anolag = 0; // 清零用于下一个通道的累加
    }
}

/**
 * @brief 将模拟量转换为数字量（判断黑白状态）
 * @param adc_value 当前读取的模拟量数组
 * @param Gray_white 判定为白色的阈值数组
 * @param Gray_black 判定为黑色的阈值数组
 * @param Digital 转换后的8位数字量结果指针（按位存储）
 */
void convertAnalogToDigital(unsigned short *adc_value, unsigned short *Gray_white, unsigned short *Gray_black,
                            unsigned char *Digital)
{
    for (int i = 0; i < 8; i++)
    {
        // 如果模拟值大于白阈值，对应位置1（判定为白）
        if (adc_value[i] > Gray_white[i])
        {
            *Digital |= (1 << i);
        }
        // 如果模拟值小于黑阈值，对应位置0（判定为黑）
        else if (adc_value[i] < Gray_black[i])
        {
            *Digital &= ~(1 << i);
        }
        // 若介于两者之间，保持原有状态不变（类似施密特触发器的滞回特性，防止抖动）
    }
}

/**
 * @brief 对模拟值进行归一化处理（映射到 0 ~ bits 范围）
 * @param adc_value 当前模拟量数组
 * @param Normal_factor 归一化比例系数数组
 * @param Calibrated_black 黑色标定基准值数组
 * @param result 归一化后的结果数组
 * @param bits ADC最大值上限（例如12位ADC为4096）
 */
void normalizeAnalogValues(unsigned short *adc_value, double *Normal_factor, unsigned short *Calibrated_black,
                           unsigned short *result, double bits)
{
    for (int i = 0; i < 8; i++)
    {
        unsigned short n;
        // 扣除黑色底噪：低于标定的黑色底噪则视为0
        if (adc_value[i] < Calibrated_black[i])
            n = 0;
        else
            n = (adc_value[i] - Calibrated_black[i]) * Normal_factor[i];

        // 限制最大值不超过设定的上限，防止溢出
        if (n > bits)
        {
            n = bits;
        }
        result[i] = n;
    }
}

/**
 * @brief 传感器初步初始化，清零所有缓存和状态变量
 * @param sensor 传感器结构体指针
 */
void No_MCU_Ganv_Sensor_Init_Frist(No_MCU_Sensor *sensor)
{
    memset(sensor->Calibrated_black, 0, 16);
    memset(sensor->Calibrated_white, 0, 16);
    memset(sensor->Normal_value, 0, 16);
    memset(sensor->Analog_value, 0, 16);

    for (int i = 0; i < 8; i++)
    {
        sensor->Normal_factor[i] = 0.0;
    }

    sensor->Digtal = 0;
    sensor->Time_out = 0;
    sensor->Tick = 0;
    sensor->ok = 0;
}

/**
 * @brief 传感器完整初始化，计算黑白动态阈值和归一化系数
 * @param sensor 传感器结构体指针
 * @param Calibrated_white 标定好的白色基准值数组
 * @param Calibrated_black 标定好的黑色基准值数组
 */
void No_MCU_Ganv_Sensor_Init(No_MCU_Sensor *sensor, unsigned short *Calibrated_white, unsigned short *Calibrated_black)
{
    No_MCU_Ganv_Sensor_Init_Frist(sensor);

    // 根据配置的ADC分辨率设定最大上限值
    if (Sensor_ADCbits == _8Bits)
        sensor->bits = 255.0;
    else if (Sensor_ADCbits == _10Bits)
        sensor->bits = 1024.0;
    else if (Sensor_ADCbits == _12Bits)
        sensor->bits = 4096.0;
    else if (Sensor_ADCbits == _14Bits)
        sensor->bits = 16384.0;

    // 根据传感器版本设置超时参数
    if (Sensor_Edition == Class)
        sensor->Time_out = 1;
    else
        sensor->Time_out = 10;

    double Normal_Diff[8];
    unsigned short temp;

    for (int i = 0; i < 8; i++)
    {
        // 确保白色标定值大于黑色标定值，否则进行交换防错
        if (Calibrated_black[i] >= Calibrated_white[i])
        {
            temp = Calibrated_white[i];
            Calibrated_white[i] = Calibrated_black[i];
            Calibrated_black[i] = temp;
        }

        // 动态计算滞回阈值：
        // 靠近白色的1/3处为判白阈值
        sensor->Gray_white[i] = (Calibrated_white[i] * 2 + Calibrated_black[i]) / 3;
        // 靠近黑色的1/3处为判黑阈值 → 改为中点，更容易识别黑线
        sensor->Gray_black[i] = (Calibrated_white[i] * 2 + Calibrated_black[i] * 3) / 5;

        sensor->Calibrated_black[i] = Calibrated_black[i];
        sensor->Calibrated_white[i] = Calibrated_white[i];

        // 防止除零异常：如果标定值全为0或黑白差值为0，则跳过系数计算
        if ((Calibrated_white[i] == 0 && Calibrated_black[i] == 0) || (Calibrated_white[i] == Calibrated_black[i]))
        {
            sensor->Normal_factor[i] = 0.0;
            continue;
        }

        // 计算归一化比例系数：(满量程) / (白-黑差值)
        Normal_Diff[i] = (double)Calibrated_white[i] - (double)Calibrated_black[i];
        sensor->Normal_factor[i] = sensor->bits / Normal_Diff[i];
    }
    sensor->ok = 1; // 标记初始化完成
}

/**
 * @brief 传感器数据处理核心任务（通常在主循环或定时器任务中周期调用）
 * 依次完成：采集模拟量 -> 转换为数字量 -> 归一化计算
 * @param sensor 传感器结构体指针
 */
void No_Mcu_Ganv_Sensor_Task_Without_tick(No_MCU_Sensor *sensor)
{
    Get_Analog_value(sensor->Analog_value);
    convertAnalogToDigital(sensor->Analog_value, sensor->Gray_white, sensor->Gray_black, &sensor->Digtal);
    normalizeAnalogValues(sensor->Analog_value, sensor->Normal_factor, sensor->Calibrated_black, sensor->Normal_value,
                          sensor->bits);
}

/**
 * @brief 获取数字量结果
 * @param sensor 传感器结构体指针
 * @return 8位数字量（每一位代表一个探头的状态）
 */
unsigned char Get_Digtal_For_User(No_MCU_Sensor *sensor)
{
    return sensor->Digtal;
}

/**
 * @brief 获取归一化结果数组
 * @param sensor 传感器结构体指针
 * @param result 存放归一化结果的数组
 * @return 1:成功获取 0:传感器未就绪
 */
unsigned char Get_Normalize_For_User(No_MCU_Sensor *sensor, unsigned short *result)
{
    if (!sensor->ok)
        return 0;
    else
    {
        memcpy(result, sensor->Normal_value, 16);
        return 1;
    }
}

/**
 * @brief 获取原始模拟量结果数组
 * @param sensor 传感器结构体指针
 * @param result 存放模拟量的数组
 * @return 1:传感器已就绪 0:传感器未就绪
 */
unsigned char Get_Anolog_Value(No_MCU_Sensor *sensor, unsigned short *result)
{
    memcpy(result, sensor->Analog_value, 16);
    if (!sensor->ok)
        return 0;
    else
        return 1;
}

/**
 * @brief 计算传感器加权偏差值，常用于PID巡线控制中的误差计算（计算灰度质心）
 * 根据传感器物理分布位置赋予不同权重（如 -7, -5, -3, -1, 1, 3, 5, 7）
 * @param sensor 传感器结构体指针
 * @param field 场地背景类型 (0:白底黑线, 1:黑底白线)
 * @return int32_t 计算出的偏差位置值。偏离中心越大，绝对值越大；正负代表左右偏离。
 */
float CalculateNormalizedValue(No_MCU_Sensor *sensor, uint8_t field)
{
    unsigned short *Normal = sensor->Normal_value;
    
    // 从左到右探头的权重分配（越靠外侧权重绝对值越大）
    // -7, -5, -3, -1, 1, 3, 5, 7

    int32_t weighted_sum = 0;
    int32_t original_sum = 0;
    static float last_value = 0.0f;

    // 动态计算底噪阈值：取满量程的 1/3
    int32_t noise_threshold = (int32_t)sensor->bits / 3;
    int32_t bits = (int32_t)sensor->bits;

    for (int i = 0; i < 8; i++)
    {
        // 1. 获取线强度：field=0(白底黑线)时，需反转；field=1(黑底白线)时，直接使用
        int32_t line_intensity = field ? Normal[i] : (bits - Normal[i]);
        
        // 2. 减去底噪死区，实现平滑过渡并彻底消除不对称
        if (line_intensity > noise_threshold) {
            line_intensity -= noise_threshold;
            
            // 3. 计算加权和，避免查表，直接使用 (2*i - 7)
            // 乘以 1024 放大误差 (左移 10 位)
            weighted_sum += line_intensity * (((i << 1) - 7) << 10);
            original_sum += line_intensity;
        }
    }

    if (original_sum != 0)
    {
        // 使用浮点数除法保留更高的计算精度，并返回浮点类型的偏差值
        last_value = (float)weighted_sum / (float)original_sum;
    }
    
    return last_value;
}

/**
 * @brief 计算数字量加权偏差值（基础版循迹算法）
 * @param Digital 8位数字量状态
 * @param field 场地背景类型(0:白底黑线, 1:黑底白线)
 * @return float 计算出的加权偏差位置。如果完全脱线，返回上一次的极限位置。
 */
int32_t CalculateDigitalWeightedValue(unsigned char Digital, uint8_t field)
{
    static int32_t last_digital_value = 0; // 记录上一次的偏差值用于脱线记忆
    
    // 如果是白底黑线(field=0)，通常数字量 0 表示黑线，1 表示白背景
    // 为了方便按位与操作，如果 field 为 0，我们将数字量取反，使得踩到黑线的位变成 1
    // 使用强制类型转换确保取反后的位数安全
    unsigned char process_digital = field ? Digital : (unsigned char)(~Digital);

    // 完全脱线时，直接返回上一次的偏差值，保持记忆效果
    if (process_digital == 0)
    {
        return last_digital_value;
    }

    int32_t sum = 0;
    int32_t count = 0;
    int32_t i = 0;

    // 仅遍历到最高位的 1，减少不必要的循环次数
    while (process_digital > 0)
    {
        // 如果该位为 1（代表该探头踩到线了）
        if (process_digital & 0x01)
        {
            sum += (i << 1) - 7; // 从左到右的权重分配：2*i - 7，替代查表
            count++;
        }
        process_digital >>= 1;
        i++;
    }

    // 计算新的偏差值并记录，避免浮点运算
    // 乘以 1024 放大误差 (左移 10 位)
    last_digital_value = (sum << 10) / count;
    return last_digital_value;
}
