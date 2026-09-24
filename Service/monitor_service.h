/**
 * @file    monitor_service.h
 * @brief   监控服务 —— 对应 ICD V1.0 第 16 节「MonitorTask 精确职责」
 *
 * 职责（ICD §2）：健康检查、超时和异常判断。
 *
 * ICD §1 冻结决定：Monitor Task 不直接计算控制输出。
 * ICD §16：V1.0 不实现复杂故障树，先实现可解释的基础保护。
 *
 * 分工（本实现有意把「判断」与「动作」分开）：
 *   MonitorService_Check() 只做判断，返回本轮发现的故障位（纯函数风格，可脱离 RTOS 单测）；
 *   MonitorTask 拿到非零故障位后执行 StopOutput + 状态转 ERROR（ICD §16）。
 *
 * 依赖（ICD §23）：Common + MotorManager。不 include FreeRTOS.h。
 */

#ifndef RC_SERVICE_MONITOR_SERVICE_H
#define RC_SERVICE_MONITOR_SERVICE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化故障寄存器与内部计时（由 main 在创建任务前调用） */
void MonitorService_Init(void);

/**
 * @brief 由 ControlTask 每拍上报心跳（ICD §16：检查 Control 心跳是否超过阈值）
 */
void MonitorService_NotifyControlHeartbeat(void);

/**
 * @brief 执行一轮健康检查
 *
 * 逐条对应 ICD §16：
 *   1) 检查 Control 心跳是否超过阈值        -> RC_FAULT_CONTROL_HB
 *   2) 检查 Motor 反馈 update_tick 是否过期 -> RC_FAULT_MOTOR_FB
 *   3) 检查 RUNNING 状态下长期无速度响应    -> RC_FAULT_NO_RESPONSE
 *   4) 检查实际速度、输出是否 NaN/Inf       -> RC_FAULT_NONFINITE
 *   5) 检查实际速度、输出是否超范围         -> RC_FAULT_OUT_OF_RANGE
 *   6) 检查命令队列是否发生过溢出           -> RC_FAULT_QUEUE_FULL
 *   7) 检查是否存在命令解析失败             -> RC_FAULT_CMD_PARSE
 *
 * @param cmd_queue_full_total   CommandService_GetSendRejectCount()  的当前值
 * @param cmd_parse_error_total  CommandService_GetParseRejectCount() 的当前值
 *        以参数传入而不是在内部调用，是为了让本模块保持零额外依赖、便于单测
 *        （MonitorService 不需要 include CommandService，依赖图上少一条边）。
 * @return 本轮新发现的故障位（可能为 RC_FAULT_NONE）
 * @note 返回的是「本轮新发现」，历史故障请用 MonitorService_GetFaults() 读取。
 */
uint32_t MonitorService_Check(uint32_t cmd_queue_full_total,
                              uint32_t cmd_parse_error_total);

/** @brief 读取累积故障寄存器（供 DebugTask 输出、供上位机解析） */
uint32_t MonitorService_GetFaults(void);

/** @brief 清零累积故障寄存器（RESET 流程的一部分，ICD §27） */
void MonitorService_ClearFaults(void);

#ifdef __cplusplus
}
#endif

#endif /* RC_SERVICE_MONITOR_SERVICE_H */
