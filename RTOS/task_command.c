/**
 * @file    task_command.c
 * @brief   CommandTask 实现 —— 对应 ICD V1.0 第 15 节
 *
 * 职责边界（ICD §11 职责分离）：
 *   本任务只做「收字节 -> 组行 -> Parse -> Send」，
 *   绝不在这里执行命令对应的系统操作（那是 CommandService_Handle 的事，
 *   由 ControlTask 在拿到队列消息后调用）。
 *
 * 关于 ISR：ICD §11 规定 ISR 只接收字节并通知任务。
 * 主机仿真里没有 ISR，BSP_UART_Read 内部用 1 ms 轮询模拟阻塞等待；
 * STM32 上由 UART 中断 + 任务通知实现同样的调用语义（BSP/bsp_uart_stm32.c）。
 */

#include "task_command.h"

#include "command_service.h"
#include "bsp_uart.h"
#include "config.h"

/* -------------------------------------------------------------------------
 * 行缓冲：固定大小，最坏情况可静态核算（ICD §20 禁止 Magic Number）
 * ---------------------------------------------------------------------- */
static char s_line[RC_CMD_LINE_MAX];

/* 一次最多读这么多字节，避免长时间占用 CPU（CommandTask 是 Medium 优先级） */
#define RC_CMD_READ_CHUNK   (16U)

/* 没有数据时的等待上限；保证任务不会永久阻塞而错过后续命令 */
#define RC_CMD_READ_TIMEOUT_MS  (20U)

/** @brief 处理一条完整命令行 */
static void TaskCommand_HandleLine(const char *line)
{
    CommandMessage msg;

    if (CommandService_Parse(line, &msg) != RC_OK) {
        /* 解析失败已由 CommandService 计数，MonitorTask 会据此置
         * RC_FAULT_CMD_PARSE。这里不做 printf：打印是 DebugTask 的职责。 */
        return;
    }

    /* 队列满时返回 RC_BUSY，计数同样由 CommandService 维护（ICD §15）。
     * 明确丢弃而不是阻塞：命令是"可丢"的，控制周期不是。 */
    (void)CommandService_Send(&msg);
}

/* =========================================================================
 * TaskCommand_Run
 * ====================================================================== */
void TaskCommand_Run(void *argument)
{
    uint32_t used = 0U;   /* s_line 中已使用的字节数（不含结尾 '\0'） */

    (void)argument;

    for (;;) {
        char     chunk[RC_CMD_READ_CHUNK];
        uint32_t got;
        uint32_t i;

        got = BSP_UART_Read(chunk, (uint32_t)RC_CMD_READ_CHUNK, RC_CMD_READ_TIMEOUT_MS);
        if (got == 0U) {
            continue;     /* 超时无数据：回到等待，不消耗 CPU 的忙等由 BSP 保证 */
        }

        for (i = 0U; i < got; ++i) {
            const char c = chunk[i];

            /* CR 与 LF 都当作行结束；多余的空行会被 Parse 判非法并被计数，
             * 因此这里对连续 CR/LF 直接跳过，避免"空行刷故障"。 */
            if ((c == '\r') || (c == '\n')) {
                if (used > 0U) {
                    s_line[used] = '\0';
                    TaskCommand_HandleLine(s_line);
                    used = 0U;
                }
                continue;
            }

            if (used < (uint32_t)(RC_CMD_LINE_MAX - 1U)) {
                s_line[used] = c;
                ++used;
            } else {
                /* 超长行：丢弃整行并重新同步，防止缓冲区溢出与被截断后
                 * "看似命中"的错误命令。 */
                used = 0U;
            }
        }
    }
}
