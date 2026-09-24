/**
 * @file    FreeRTOS.h
 * @brief   主机仿真用的 FreeRTOS 兼容层（基础类型与配置）
 *
 * 【这是什么】
 * ICD §0 的目标是「一周内可完成、可编译、可仿真、可展示」，且默认 Simulation Mode。
 * 本文件让 RTOS/ 下的任务代码在 PC 上编译并运行，
 * 而任务代码本身（task_control.c 等）一字不改 —— 换到 STM32 时只用真实
 * FreeRTOS-Kernel 的头文件替换本目录，任务源码不需要任何 #ifdef。
 *
 * 【仿真调度器的语义，务必看清】
 *   本仿真器是「确定性单线程调度」：
 *     - 只有显式调用 vTaskDelay / vTaskDelayUntil / 阻塞式队列操作才会切换任务；
 *     - tick 以 1 ms 递增，与 configTICK_RATE_HZ = 1000 一致；
 *     - 同优先级任务按创建顺序运行；
 *     - 不使用真实线程，因此不存在抢占抖动，仿真结果可逐拍复现。
 *   这样做的目的：让「测试数据」可复现、可回归，并且不受宿主机器负载影响。
 *   真实抢占行为由 STM32 + FreeRTOS-Kernel 承担（RTOS/port_freertos/）。
 */

#ifndef RC_PORT_SIM_FREERTOS_H
#define RC_PORT_SIM_FREERTOS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "config.h"   /* 任务优先级等数值的唯一来源（ICD §12 / §20） */

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t      TickType_t;
typedef long          BaseType_t;
typedef unsigned long UBaseType_t;
typedef void *        TaskHandle_t;
typedef void *        QueueHandle_t;

#define pdTRUE       ((BaseType_t)1)
#define pdFALSE      ((BaseType_t)0)
#define pdPASS       pdTRUE
#define pdFAIL       pdFALSE

#define errQUEUE_FULL  ((BaseType_t)-1)

/* 1 ms tick：与 ICD §20 的 10 / 100 / 500 ms 周期整除，无取整误差 */
#define configTICK_RATE_HZ    (1000U)
#define portTICK_PERIOD_MS    (1U)
#define pdMS_TO_TICKS(ms)     ((TickType_t)(ms))
#define portMAX_DELAY         ((TickType_t)0xFFFFFFFFU)

/* 优先级上限与任务优先级数值统一来自 Common/config.h（ICD §12 的要求） */
#define configMAX_PRIORITIES  (RC_CONFIG_MAX_PRIORITIES)
#define tskIDLE_PRIORITY      (0U)

typedef void (*TaskFunction_t)(void *);

#ifdef __cplusplus
}
#endif

#endif /* RC_PORT_SIM_FREERTOS_H */
