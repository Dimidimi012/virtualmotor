/**
 * @file    task_control.h
 * @brief   ControlTask —— 对应 ICD V1.0 第 13 节「ControlTask 精确职责」
 *
 * ICD §1 关键冻结决定：Control Task 是唯一 PID 执行者。
 * ICD §6：PID_Update 只允许 Control Task 调用。
 *
 * PID 实例的所有权：本任务持有唯一的 PIDController（见 task_control.c）。
 * 之所以提供 TaskControl_InitController() 而不是把 PID 实例放到 main 里，
 * 是为了让「谁拥有 PID」和「谁调用 PID_Update」在代码上就是同一个编译单元。
 * 该函数占据 ICD §22 启动顺序中 PID_Init 的位置。
 */

#ifndef RC_RTOS_TASK_CONTROL_H
#define RC_RTOS_TASK_CONTROL_H

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化 PID（ICD §22 启动顺序中的 PID_Init 步骤） */
void TaskControl_InitController(void);

/** @brief 任务主体，由 App_CreateTasks 通过 xTaskCreate 启动 */
void TaskControl_Run(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* RC_RTOS_TASK_CONTROL_H */
