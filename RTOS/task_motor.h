/**
 * @file    task_motor.h
 * @brief   MotorTask —— 对应 ICD V1.0 第 14 节「MotorTask 精确职责」
 *
 * ICD §1 关键冻结决定：Motor Task 是唯一电机模型更新者。
 * ICD §18 规则 2：VirtualMotor 仅 MotorTask 写入。
 *
 * VirtualMotor 实例的所有权：本任务持有唯一的 VirtualMotor（见 task_motor.c）。
 * TaskMotor_InitModel() 占据 ICD §22 启动顺序中 VirtualMotor_Init 的位置。
 */

#ifndef RC_RTOS_TASK_MOTOR_H
#define RC_RTOS_TASK_MOTOR_H

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化/复位虚拟电机（ICD §22 启动顺序中的 VirtualMotor_Init 步骤） */
void TaskMotor_InitModel(void);

/** @brief 任务主体 */
void TaskMotor_Run(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* RC_RTOS_TASK_MOTOR_H */
