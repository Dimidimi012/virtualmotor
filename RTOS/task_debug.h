/**
 * @file    task_debug.h
 * @brief   DebugTask —— 对应 ICD V1.0 第 17 节「DebugTask 精确职责」
 *
 * ICD §17 建议输出：
 *     TGT=1000.0 ACT=932.5 OUT=42.8 POS=123.4 STATE=RUNNING
 * ICD §17：DebugTask 是低优先级消费者；UART 发送必须避免长时间阻塞 ControlTask；
 *          V1.0 若使用简单阻塞发送，也必须仅在 DebugTask 中执行。
 * ICD §18 规则 7：Debug 不得持有共享锁后执行 UART 输出。
 * ICD §12：DebugTask 优先级 Low，周期 500 ms。
 */

#ifndef RC_RTOS_TASK_DEBUG_H
#define RC_RTOS_TASK_DEBUG_H

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 任务主体 */
void TaskDebug_Run(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* RC_RTOS_TASK_DEBUG_H */
