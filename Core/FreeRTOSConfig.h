/**
 * @file    FreeRTOSConfig.h
 * @brief   FreeRTOS 内核配置（STM32 Hardware Mode）
 *
 * 【关键设计：任务优先级只有一个来源】
 * ICD §12 要求"建议初始优先级使用相对宏定义……具体数字由 RTOS 配置统一定义"。
 * 本文件因此 include Common/config.h，把 configMAX_PRIORITIES 与任务优先级
 * 都绑定到那一处，而不是在这里再抄一份数字。
 * 主机仿真侧的 RTOS/port_sim/FreeRTOS.h 做了完全相同的事 ——
 * 这样"仿真的任务优先级等于硬件的任务优先级"是结构保证，不是口头约定。
 *
 * 【验证状态】
 *   未在当前环境编译验证（缺少 arm-none-eabi 工具链与 FreeRTOS-Kernel 源码）。
 *   configCPU_CLOCK_HZ 必须按实际时钟树修改，否则 tick 周期不会是 1 ms。
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "config.h"   /* 任务优先级 / 栈深度 / 周期的唯一来源 */

/* ---------------------------------------------------------------- 调度器 */
#define configUSE_PREEMPTION                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1
#define configUSE_TICKLESS_IDLE                  0
#define configUSE_TIME_SLICING                   1

/* TODO(移植)：改成实际 SYSCLK。写错这个值，10 ms 就变成别的东西，
 * 而现象是"控制环好像有点慢"，极难定位。 */
#define configCPU_CLOCK_HZ                       (168000000UL)

/* 1 ms tick：与 ICD §20 的 10 / 100 / 500 ms 周期整除，无取整误差 */
#define configTICK_RATE_HZ                       ((TickType_t)1000)

#define configMAX_PRIORITIES                     (RC_CONFIG_MAX_PRIORITIES)
#define configMINIMAL_STACK_SIZE                 ((unsigned short)128)
#define configMAX_TASK_NAME_LEN                  (8)
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  1

/* ---------------------------------------------------------------- 内存 */
#define configSUPPORT_STATIC_ALLOCATION          0
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configTOTAL_HEAP_SIZE                    ((size_t)(20U * 1024U))
#define configAPPLICATION_ALLOCATED_HEAP         0

/* ---------------------------------------------------------------- 同步原语
 * ICD §18 规则 1：PIDController 仅 ControlTask 访问，V1.0 不需要 Mutex。
 * 因此这里把互斥量、信号量全部关掉 —— 配置本身就是"我们不需要它们"的证据。 */
#define configUSE_MUTEXES                        0
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            0
#define configUSE_QUEUE_SETS                     0
#define configUSE_TASK_NOTIFICATIONS             1
#define configQUEUE_REGISTRY_SIZE                4

/* ---------------------------------------------------------------- 钩子与诊断 */
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configUSE_MALLOC_FAILED_HOOK             1
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_TRACE_FACILITY                 0
#define configGENERATE_RUN_TIME_STATS            0
#define configUSE_STATS_FORMATTING_FUNCTIONS     0
#define configUSE_APPLICATION_TASK_TAG           0

/* ---------------------------------------------------------------- 断言
 * ICD §21：V1.0 不使用大量 assert 作为运行时保护，因为发布版本可能关闭 assert。
 * 这里保留 configASSERT 只用于"启动期配置错误"这类不可恢复的情况，
 * 关键安全路径一律用明确的错误码（RC_Result），不依赖断言。 */
extern void RC_Port_AssertFailed(const char *file, int line);
#define configASSERT(x)   if ((x) == 0) { RC_Port_AssertFailed(__FILE__, __LINE__); }

/* ---------------------------------------------------------------- 中断优先级
 * Cortex-M4 用 4 位优先级（configPRIO_BITS 由端口定义）。
 * 数值越大优先级越低。凡调用 FromISR API 的中断，
 * 其抢占优先级数值必须 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY。 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY        15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY   5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - 4))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - 4))

/* ---------------------------------------------------------------- 可选 API */
#define INCLUDE_vTaskPrioritySet                 1
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_xTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_xTaskGetIdleTaskHandle           1
#define INCLUDE_eTaskGetState                    1

/* ---------------------------------------------------------------- 端口映射
 * 注意：一旦在这里做了下面的映射，就必须把 stm32f4xx_it.c 里
 * CubeMX 生成的 SVC_Handler / PendSV_Handler / SysTick_Handler 删除，
 * 否则会出现重复定义；或者反过来，保留 CubeMX 版本、不在这里映射。
 * 两者选其一，不能同时存在 —— 这是 FreeRTOS + CubeMX 最常见的链接错误。 */
#define vPortSVCHandler        SVC_Handler
#define xPortPendSVHandler     PendSV_Handler
#define xPortSysTickHandler    SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
