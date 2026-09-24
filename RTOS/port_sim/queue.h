/**
 * @file    queue.h
 * @brief   主机仿真用的 FreeRTOS 队列 API 子集
 *
 * 数据语义与真实 FreeRTOS 一致：按值拷贝，不是按引用传递。
 * 这正是 ICD §5「CommandQueue 只传递值，不传递裸指针」能够成立的前提。
 */

#ifndef RC_PORT_SIM_QUEUE_H
#define RC_PORT_SIM_QUEUE_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);

/** @brief 发送（拷贝入队）；满且 xTicksToWait==0 时返回 errQUEUE_FULL */
BaseType_t xQueueSend(QueueHandle_t xQueue,
                      const void   *pvItemToQueue,
                      TickType_t    xTicksToWait);

/** @brief 接收（拷贝出队）；空且 xTicksToWait==0 时返回 pdFALSE */
BaseType_t xQueueReceive(QueueHandle_t xQueue,
                         void         *pvBuffer,
                         TickType_t    xTicksToWait);

/** @brief 当前队列中的消息条数 */
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue);

#ifdef __cplusplus
}
#endif

#endif /* RC_PORT_SIM_QUEUE_H */
