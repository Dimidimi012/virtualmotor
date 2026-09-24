/**
 * @file    task_monitor.c
 * @brief   MonitorTask 实现 —— 对应 ICD V1.0 第 16 节
 *
 * 分工（有意为之）：
 *   MonitorService_Check() 只做判断，返回故障位；
 *   本任务负责执行动作（StopOutput + 转 ERROR）。
 * 这样「判断逻辑」可以脱离 RTOS 单测（见 Tests/test_monitor_service.c），
 * 而「动作」这一层薄到几乎不可能出错。
 *
 * ICD §1：Monitor Task 不直接计算控制输出 —— 它只会把输出清零，不会写入任何控制量。
 */

#include "task_monitor.h"

#include "monitor_service.h"
#include "command_service.h"
#include "motor_manager.h"
#include "config.h"

/* =========================================================================
 * TaskMonitor_Run
 * ====================================================================== */
void TaskMonitor_Run(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)argument;

    for (;;) {
        uint32_t found;

        vTaskDelayUntil(&last_wake, (TickType_t)pdMS_TO_TICKS(RC_MONITOR_PERIOD_MS));

        /* 命令健康计数以参数注入，MonitorService 因而不依赖 CommandService */
        found = MonitorService_Check(CommandService_GetSendRejectCount(),
                                      CommandService_GetParseRejectCount());

        /* 只有「闭环已不可信」这一类故障才触发保护动作（见 config.h B8b）。
         * 命令层面的故障（队列满、解析失败）已在 MonitorService 里被记录，
         * 这里刻意不停机：串口打错一个字不应该让电机急停。 */
        if ((found & RC_FAULT_SEVERE_MASK) != 0U) {
            /* ICD §16：严重错误 -> StopOutput -> 状态转 ERROR
             * 顺序不能颠倒：先切断执行器，再改状态。 */
            MotorManager_StopOutput();
            (void)MotorManager_SetState(MOTOR_ERROR);
        }
    }
}
