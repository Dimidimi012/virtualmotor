/**
 * @file    task.h
 * @brief   主机仿真用的 FreeRTOS 任务 API 子集
 *
 * 只实现 RoboControl V1.0 实际用到的调用，保持仿真层最小、
 * 从而让「任务代码用到哪些 RTOS 能力」这件事一望而知。
 */

#ifndef RC_PORT_SIM_TASK_H
#define RC_PORT_SIM_TASK_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 创建任务；本仿真不使用 pvParameters（保持与真实 FreeRTOS 同签名） */
BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char    *pcName,
                       uint16_t       usStackDepth,
                       void          *pvParameters,
                       UBaseType_t    uxPriority,
                       TaskHandle_t  *pxCreatedTask);

/** @brief 相对延时；ticks == 0 时按 1 拍处理，避免"不推进 tick 的忙等" */
void vTaskDelay(TickType_t xTicksToDelay);

/** @brief 绝对周期延时，保证固定周期（ICD §13 / §14 要求的 10 ms 节拍） */
void vTaskDelayUntil(TickType_t *pxPreviousWakeTime, TickType_t xTimeIncrement);

/** @brief 启动调度器；本仿真中在到达 tick 上限后返回（见 sim_rtos.h） */
void vTaskStartScheduler(void);

/** @brief 挂起任务（用于 ICD §27 的故障注入：停止 Motor 更新） */
void vTaskSuspend(TaskHandle_t xTaskToSuspend);

/** @brief 恢复任务 */
void vTaskResume(TaskHandle_t xTaskToResume);

/** @brief 当前 tick（ms） */
TickType_t xTaskGetTickCount(void);

/** @brief 结束任务（本仿真中仅把任务从就绪集合移除） */
void vTaskDelete(TaskHandle_t xTaskToDelete);

#ifdef __cplusplus
}
#endif

#endif /* RC_PORT_SIM_TASK_H */
