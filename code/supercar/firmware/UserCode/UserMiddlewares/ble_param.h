#ifndef __BLE_PARAM_H
#define __BLE_PARAM_H

/**
 * @file    ble_param.h
 * @brief   蓝牙参数表管理
 *
 * 所有可通过 BLE 读写的参数在此注册。
 * 每个参数绑定到一个 float* 指针, 支持读/写/恢复默认/Flash 持久化。
 */

#include <stdint.h>
#include <stdbool.h>

/* 参数 ID */
enum {
    PARAM_PID_SPEED_KP = 0x00,
    PARAM_PID_SPEED_KI = 0x01,
    PARAM_PID_SPEED_KD = 0x02,
    PARAM_PID_SPEED_FF = 0x03,
    PARAM_PID_LINE_KP  = 0x04,
    PARAM_PID_LINE_KD  = 0x05,
    PARAM_PID_ANGLE_KP = 0x06,
    PARAM_PID_ANGLE_KD = 0x07,
    PARAM_BASE_SPEED   = 0x08,
    PARAM_TURN_INNER   = 0x09,
    PARAM_TURN_OUTER   = 0x0A,
    PARAM_TURN_TARGET  = 0x0B,
    PARAM_SOFT_START   = 0x0C,
    PARAM_LAPS         = 0x0D,
    PARAM_GRAY_THRESH  = 0x0E,
    PARAM_GYRO_BIAS_X  = 0x10,
    PARAM_GYRO_BIAS_Y  = 0x11,
    PARAM_GYRO_BIAS_Z  = 0x12,
    PARAM_COUNT
};

typedef struct {
    uint8_t  id;
    const char *name;
    float    *ptr;
    float    def_val;
    float    min_val;
    float    max_val;
} param_entry_t;

/* 速度节点曲线。speed_cm_s 必须递增；每个节点均是可直接导出为 C 的赛前参数。 */
#define PID_SCHEDULE_NODE_COUNT 3U

typedef struct {
    float speed_cm_s;
    float speed_kp;
    float speed_ki;
    float speed_kd;
    float speed_kf;
    float line_kp;
    float line_kd;
    float line_max_steer;
    /* 曲线段增益：依据归一化循迹误差连续混入，不改变 MCU 本地闭环周期。 */
    float curve_kp;
    float curve_kd;
    float curve_max_steer;
    float curve_error_trigger;
    float yaw_kp;
    float yaw_kd;
    float yaw_max_steer;
    float pos_kp;
    float decel_pulses;
    float end_speed;
    /* 编码器距离到真实地面距离的速度节点校正系数，1.0 表示标称轮径。 */
    float distance_scale;
} pid_schedule_node_t;

extern param_entry_t g_param_table[];
extern const uint8_t g_param_count;

/* ==================== API ==================== */

/**
 * 初始化参数表。
 * 必须在此函数调用后参数方可读写。
 */
void BLE_Param_Init(void);

/**
 * 参数读取 (通过 ID)
 * 返回当前值。ID 无效时返回 0.0f。
 */
float BLE_Param_Read(uint8_t id);

/**
 * 参数写入 (通过 ID)
 * 自动限幅到 [min, max]。返回 true 写入成功。
 */
bool BLE_Param_Write(uint8_t id, float value);

/**
 * 恢复所有参数为默认值
 */
void BLE_Param_LoadDefaults(void);

/**
 * 获取参数表条目 (供 PARAM_LIST 命令用)
 * @param index  0-based 索引
 * @param name_out  输出参数名 (最多 23 字符 + '\0')
 * @param val_out   输出当前值
 * @param def_out   输出默认值
 * @param min_out / max_out 输出范围
 * @return true 表示有效条目, false 表示越界
 */
bool BLE_Param_GetEntry(uint8_t index,
                        char *name_out, float *val_out,
                        float *def_out, float *min_out, float *max_out);

/**
 * Flash 持久化:
 *   保存:  将所有参数值写入 Flash (2KB 末尾扇区)
 *   加载:  从 Flash 恢复参数值, 失败则保持当前值
 *
 *   Flash 写入需要擦除扇区, 约耗时 20-50ms, 请在空闲时调用。
 */
bool BLE_Param_SaveToFlash(void);
bool BLE_Param_LoadFromFlash(void);

/* 便捷访问器 (供 app_ble / app_teach 等直接读取运动参数) */
float BLE_Param_GetBaseSpeed(void);
float BLE_Param_GetTurnInner(void);
float BLE_Param_GetTurnOuter(void);
float BLE_Param_GetTurnTarget(void);
float BLE_Param_GetSoftStart(void);
float BLE_Param_GetLaps(void);
float BLE_Param_GetGrayThresh(void);

/* 速度自适应曲线：Write 仅修改暂存表；Commit 会在安全状态下一次性切换活动表。 */
const pid_schedule_node_t *BLE_Param_GetPidSchedule(void);
const pid_schedule_node_t *BLE_Param_GetPidSchedulePending(void);
bool BLE_Param_WritePidScheduleNode(uint8_t index, const pid_schedule_node_t *node);
bool BLE_Param_CommitPidSchedule(void);
void BLE_Param_ResetPidSchedulePending(void);
/* 将单次速度阶跃整定结果写入最接近 test_speed_cm_s 的节点后原子提交。 */
bool BLE_Param_AutoTuneSpeedNode(float test_speed_cm_s, float kp, float ki, float kd, float kf);

/* 根据目标基础速度（cm/s）对活动曲线做分段线性插值。 */
void BLE_Param_InterpolatePidSchedule(float speed_cm_s, pid_schedule_node_t *out);

#endif /* __BLE_PARAM_H */
