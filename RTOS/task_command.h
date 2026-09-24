/**
 * @file    task_command.h
 * @brief   CommandTask —— 对应 ICD V1.0 第 15 节「CommandTask 精确职责」
 *
 * ICD §15：CommandTask 是唯一字符串命令入口。
 *          推荐 UART 命令：START、STOP、SET_SPEED 1000、RESET、STATUS。
 *          命令解析完成后构造 CommandMessage 并发送 CommandQueue。
 *          Queue 满时不得无限阻塞，应记录错误或丢弃非关键重复命令。
 *
 * ICD §12：CommandTask 是 Event-driven，不是周期任务。
 */

#ifndef RC_RTOS_TASK_COMMAND_H
#define RC_RTOS_TASK_COMMAND_H

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 任务主体 */
void TaskCommand_Run(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* RC_RTOS_TASK_COMMAND_H */
