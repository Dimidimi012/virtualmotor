/**
 * @file    port_freertos.c
 * @brief   RC_Port_* 平台原语在真实 FreeRTOS 上的实现（STM32 Hardware Mode）
 *
 * 【这份文件是"预留 Hardware Mode"的关键一环】
 * Common/rc_port.h 声明了三个平台原语。主机仿真由 RTOS/port_sim/port_sim.c 实现；
 * STM32 上就由本文件实现。两者替换时，Service 层与 Task 层一行都不用改。
 *
 * 【验证状态 —— 必须如实说明】
 *   本文件未在当前环境编译验证（本机没有 arm-none-eabi 工具链、STM32 HAL 与
 *   FreeRTOS-Kernel 源码）。它是接口占位实现，逻辑很薄、依赖很明确，
 *   但仍必须在真实工程里至少编译与运行一次，见 docs/BUILD_STM32.md 的验收步骤。
 */

#include "rc_port.h"
#include "config.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* -------------------------------------------------------------------------
 * 命令队列句柄
 * ---------------------------------------------------------------------- */
static QueueHandle_t s_cmd_queue;

/* =========================================================================
 * Tick
 * ====================================================================== */
uint32_t RC_Port_GetTick(void)
{
    /* FreeRTOS 的 TickType_t 在 32 位端口上是 uint32_t，且天然回绕，
     * 正好匹配 ICD §4 对 update_tick 的语义与 MonitorService 的回绕安全比较。 */
    return (uint32_t)xTaskGetTickCount();
}

/* =========================================================================
 * 临界区（ICD §18 规则 5）
 *
 * 选择 taskENTER_CRITICAL 而不是 taskENTER_CRITICAL_FROM_ISR：
 *   MotorManager_GetSnapshot 只在任务上下文被调用。
 * 选择它而不是挂起调度器，是因为快照拷贝只有几条指令，
 * 关中断的开销可以忽略，而且不会影响中断响应。
 * ====================================================================== */
void RC_Port_EnterCritical(void)
{
    taskENTER_CRITICAL();
}

void RC_Port_ExitCritical(void)
{
    taskEXIT_CRITICAL();
}

/* =========================================================================
 * 命令队列（ICD §5 / §11 / §15）
 * ====================================================================== */
void RC_Port_CmdQueueCreate(void)
{
    s_cmd_queue = xQueueCreate((UBaseType_t)RC_CMD_QUEUE_LENGTH,
                               (UBaseType_t)sizeof(CommandMessage));

    /* 队列创建失败在 FreeRTOS 里返回 NULL，而 ICD §22 要求启动即失败，
     * 不允许"带着半个系统"进入调度器。 */
    configASSERT(s_cmd_queue != NULL);
}

RC_Result RC_Port_CmdQueueSend(const CommandMessage *msg)
{
    if ((s_cmd_queue == 0) || (msg == 0)) {
        return RC_ERROR;
    }

    /* ICD §15：Queue 满时不得无限阻塞。
     * RC_CMD_QUEUE_SEND_TIMEOUT_TICKS 默认为 0，即"绝不阻塞、
     * 满就立刻返回 RC_BUSY"，由 CommandService 计数、MonitorTask 记录。 */
    if (xQueueSend(s_cmd_queue, msg,
                   (TickType_t)RC_CMD_QUEUE_SEND_TIMEOUT_TICKS) != pdTRUE) {
        return RC_BUSY;
    }
    return RC_OK;
}

RC_Result RC_Port_CmdQueueReceive(CommandMessage *msg)
{
    if ((s_cmd_queue == 0) || (msg == 0)) {
        return RC_ERROR;
    }

    /* ControlTask 的取队列必须是"非阻塞的循环取空"（ICD §13 第 2 条），
     * 因此这里固定用 0 超时。 */
    if (xQueueReceive(s_cmd_queue, msg, (TickType_t)0) != pdTRUE) {
        return RC_TIMEOUT;
    }
    return RC_OK;
}
