/**
 * @file    task_debug.c
 * @brief   DebugTask 实现 —— 对应 ICD V1.0 第 17 节
 *
 * 两个关键纪律：
 *   1) 只在拿到快照之后才格式化输出。MotorManager_GetSnapshot() 内部使用短临界区，
 *      格式化与 UART 发送都发生在临界区之外（ICD §18 规则 7）。
 *   2) 本任务是唯一允许 printf/UART 输出的任务。ControlTask 绝不打印。
 */

#include "task_debug.h"

#include "motor_manager.h"
#include "monitor_service.h"
#include "command_service.h"
#include "bsp_uart.h"
#include "config.h"

#include <stdio.h>

/* =========================================================================
 * 内部：格式化并输出一行（ICD §17 的冻结格式 + 故障位）
 * ====================================================================== */
static void TaskDebug_PrintState(const MotorData *snap)
{
    char line[128];
    int  n;

    /* 说明：float 在 newlib-nano 上默认不参与 printf 格式化，STM32 构建
     * 需要在 FreeRTOSConfig/链接选项里打开 -u _printf_float。
     * 这一点写在 docs/BUILD_STM32.md 的"已知注意事项"里，
     * 否则现场会看到一堆空的 %f —— 那是最典型的"代码没错但什么都看不到"。 */
    n = snprintf(line, sizeof(line),
                 "TGT=%.1f ACT=%.1f OUT=%.2f POS=%.1f STATE=%s FAULT=0x%08lX",
                 (double)snap->target_speed,
                 (double)snap->actual_speed,
                 (double)snap->control_output,
                 (double)snap->position,
                 MotorState_Name(snap->state),
                 (unsigned long)MonitorService_GetFaults());

    if ((n > 0) && (n < (int)sizeof(line))) {
        BSP_UART_WriteLine(line);
    }
}

/* =========================================================================
 * TaskDebug_Run
 * ====================================================================== */
void TaskDebug_Run(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)argument;

    for (;;) {
        MotorData snap;

        vTaskDelayUntil(&last_wake, (TickType_t)pdMS_TO_TICKS(RC_DEBUG_PERIOD_MS));

        /* STATUS 命令要求"立刻打印一次"：ControlTask 不打印，只置标志，
         * 由本任务取走并输出（ICD §13 禁止 ControlTask printf）。 */
        if (CommandService_TakeStatusRequest()) {
            snap = MotorManager_GetSnapshot();
            BSP_UART_WriteLine("[STATUS] requested by operator");
            TaskDebug_PrintState(&snap);
            continue;
        }

        snap = MotorManager_GetSnapshot();
        TaskDebug_PrintState(&snap);
    }
}
