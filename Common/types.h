/**
 * @file    types.h
 * @brief   RoboControl V1.0 全局公共类型  —— 对应 ICD V1.0 第 3 / 4 / 5 节（冻结）
 *
 * 边界约定：
 *   ICD §2  : Common 只放公共类型、配置、错误码、运行模式，禁止放业务逻辑。
 *   ICD §3  : 禁止各模块自行定义另一套 MotorState 或 CommandType；
 *             公共枚举只能由 Common 模块统一维护。
 *   ICD §23 : Common 不被任何模块反向依赖，且自身不依赖 FreeRTOS / HAL / UART。
 *
 * 本文件是纯头文件，可脱离 RTOS 在 PC 上独立编译（ICD §28 阶段 A 的前提）。
 */

#ifndef RC_COMMON_TYPES_H
#define RC_COMMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * RC_Result —— 全工程统一返回码（ICD §3 冻结）
 * 使用约定见 ICD §21「错误处理策略」。
 * ====================================================================== */
typedef enum {
    RC_OK = 0,          /**< 成功                                    */
    RC_ERROR,           /**< 通用失败                                */
    RC_INVALID_PARAM,   /**< 参数非法 / 非法状态转换（ICD §10、§21） */
    RC_TIMEOUT,         /**< 超时（ICD §21）                         */
    RC_BUSY             /**< 资源忙，例如 Queue 满（ICD §21）        */
} RC_Result;

/* =========================================================================
 * RC_RunMode —— 运行模式（ICD §3 冻结）
 * ICD §0：默认 Simulation Mode；预留 Hardware Mode。
 * ====================================================================== */
typedef enum {
    RC_MODE_SIMULATION = 0,   /**< 虚拟电机仿真，无需硬件（V1.0 默认） */
    RC_MODE_HARDWARE          /**< 真实电机驱动（V1.0 仅预留接口）     */
} RC_RunMode;

/* =========================================================================
 * MotorState —— 电机状态机状态（ICD §3 冻结；合法转换见 ICD §10）
 * ====================================================================== */
typedef enum {
    MOTOR_INIT = 0,     /**< 初始化中           */
    MOTOR_READY,        /**< 就绪，可接受 START */
    MOTOR_RUNNING,      /**< 闭环运行中         */
    MOTOR_STOPPED,      /**< 已停止，输出为 0   */
    MOTOR_ERROR         /**< 故障，输出被强制 0 */
} MotorState;

/* =========================================================================
 * CommandType —— 命令类型（ICD §3 冻结）
 * 与 UART 文本命令的映射见 ICD §15 与 Service/command_service.c。
 * ====================================================================== */
typedef enum {
    CMD_NONE = 0,       /**< 空命令 / 解析未命中 */
    CMD_START,          /**< START               */
    CMD_STOP,           /**< STOP                */
    CMD_SET_SPEED,      /**< SET_SPEED <value>   */
    CMD_RESET,          /**< RESET               */
    CMD_STATUS          /**< STATUS              */
} CommandType;

/* =========================================================================
 * MotorData —— 电机统一数据对象（ICD §4 冻结）
 *
 * 所有权规则（ICD §4，必须遵守）：
 *   - MotorManager 拥有 MotorData 的唯一存储；
 *   - 其他模块不得保存并长期修改 MotorData 指针；
 *   - Control Task 通过快照读取反馈，并通过 API 提交控制输出；
 *   - Motor Task 通过 API 更新反馈。
 * 因此本结构体只应通过 MotorManager_GetSnapshot() 以「值拷贝」方式流出。
 * ====================================================================== */
typedef struct {
    float      target_speed;    /**< 目标速度，Control 读取（ICD §4）          */
    float      actual_speed;    /**< 实际速度反馈                              */
    float      position;        /**< 虚拟或真实编码器位置                      */
    float      control_output;  /**< PID 输出                                  */
    MotorState state;           /**< 电机状态                                  */
    uint32_t   update_tick;     /**< 最近一次反馈更新时间（RTOS tick，ICD §4） */
} MotorData;

/* =========================================================================
 * CommandMessage —— CommandQueue 冻结格式（ICD §5）
 *
 * ICD §5：V1.0 CommandQueue 只传递值，不传递裸指针。
 * 原因：FreeRTOS Queue 会复制数据，值传递可以避免生命周期错误。
 * ====================================================================== */
typedef struct {
    CommandType type;       /**< 命令类型                       */
    float       value;      /**< 命令参数（仅 SET_SPEED 使用）  */
    uint32_t    timestamp;  /**< 入队时的 RTOS tick             */
} CommandMessage;

/* =========================================================================
 * 轻量工具函数
 * 说明：全部为 static inline，保证 Common 保持「纯头文件、零链接依赖」，
 *       便于 ICD §28 阶段 A 在 PC 上单独编译测试，也避免占用 MCU Flash。
 * ====================================================================== */

/** @brief RC_Result 的可读名字，仅用于调试输出（禁止在 ControlTask 中调用） */
static inline const char *RC_Result_Name(RC_Result r)
{
    switch (r) {
        case RC_OK:            return "RC_OK";
        case RC_ERROR:         return "RC_ERROR";
        case RC_INVALID_PARAM: return "RC_INVALID_PARAM";
        case RC_TIMEOUT:       return "RC_TIMEOUT";
        case RC_BUSY:          return "RC_BUSY";
        default:               return "RC_?";
    }
}

/** @brief MotorState 的可读名字，仅用于调试输出 */
static inline const char *MotorState_Name(MotorState s)
{
    switch (s) {
        case MOTOR_INIT:    return "INIT";
        case MOTOR_READY:   return "READY";
        case MOTOR_RUNNING: return "RUNNING";
        case MOTOR_STOPPED: return "STOPPED";
        case MOTOR_ERROR:   return "ERROR";
        default:            return "?";
    }
}

/** @brief CommandType 的可读名字，仅用于调试输出 */
static inline const char *CommandType_Name(CommandType c)
{
    switch (c) {
        case CMD_NONE:      return "CMD_NONE";
        case CMD_START:     return "CMD_START";
        case CMD_STOP:      return "CMD_STOP";
        case CMD_SET_SPEED: return "CMD_SET_SPEED";
        case CMD_RESET:     return "CMD_RESET";
        case CMD_STATUS:    return "CMD_STATUS";
        default:            return "CMD_?";
    }
}

/**
 * @brief 判断浮点数是否为有限值（非 NaN、非 +/-Inf）
 *
 * ICD §16：MonitorTask 需要检查实际速度、输出是否 NaN/Inf。
 * 不使用 math.h 的 isfinite()，原因：
 *   1) 避免 Algorithm/Common 层引入 libm 依赖（ICD §23）；
 *   2) 在 -ffast-math 之外的标准编译下行为确定。
 * NaN  : (x == x) 为假           -> 立即返回 false
 * +/-Inf: (x - x) 为 NaN，!= 0   -> 返回 false
 */
static inline bool RC_IsFinite(float x)
{
    return (x == x) && ((x - x) == 0.0f);
}

#ifdef __cplusplus
}
#endif

#endif /* RC_COMMON_TYPES_H */
