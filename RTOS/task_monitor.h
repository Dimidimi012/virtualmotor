/**
 * @file    task_monitor.h
 * @brief   MonitorTask —— 对应 ICD V1.0 第 16 节「MonitorTask 精确职责」
 *
 * ICD §16：检查 Control 心跳、Motor 反馈过期、RUNNING 长期无响应、
 *          速度/输出 NaN/Inf 或超范围；
 *          严重错误：MotorManager_StopOutput -> 状态转 ERROR。
 * ICD §12：MonitorTask 优先级 Low，周期 100 ms。
 * ICD §1：Monitor Task 不直接计算控制输出。
 */

#ifndef RC_RTOS_TASK_MONITOR_H
#define RC_RTOS_TASK_MONITOR_H

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 任务主体 */
void TaskMonitor_Run(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* RC_RTOS_TASK_MONITOR_H */
