/**
 * @file    bsp_uart_stm32.c
 * @brief   串口 BSP 的 STM32 HAL 实现（STM32 Hardware Mode）
 *
 * 【设计要点 —— 严格对应 ICD §11】
 *   "UART ISR 不允许执行复杂解析和 printf。ISR 只接收字节/通知任务。"
 *   本实现让 ISR 只做两件事：把字节丢进环形缓冲、用任务通知唤醒 CommandTask。
 *   解析、组行、printf 全部发生在任务上下文。
 *
 * 【为什么用「中断 + 环形缓冲 + 任务通知」而不是 DMA + 空闲中断】
 *   V1.0 的命令是低频、短报文（一行 "SET_SPEED 1000" 二十几个字节），
 *   逐字节中断的 CPU 开销完全可接受，而代码量、可读性和可调试性都更好。
 *   DMA 空闲中断版本的收益要等到遥测高频上行时才体现，属于 V1.1。
 *
 * 【验证状态】
 *   未在当前环境编译验证（缺少 arm-none-eabi 工具链 / HAL / FreeRTOS 源码）。
 *   见 docs/BUILD_STM32.md 的验收步骤与已知注意事项。
 */

#include "bsp_uart.h"

#include "FreeRTOS.h"
#include "task.h"

#include "stm32f4xx_hal.h"   /* 按实际工程替换为对应系列头文件 */

#include <string.h>

/* =========================================================================
 * 硬件绑定 —— 集中在一处，换板子只改这一块
 *
 * TODO(移植): 下面三处必须按实际工程修改：
 *   1) BSP_UART_HANDLE 指向 CubeMX 生成的 UART 句柄；
 *   2) 使能对应的 USARTx_IRQn 并实现 USARTx_IRQHandler（见 Core/stm32f4xx_it.c）；
 *   3) 确认波特率与上位机一致（RC_DEBUG_UART_BAUD）。
 * ====================================================================== */
extern UART_HandleTypeDef huart2;          /* TODO(移植) */
#define BSP_UART_HANDLE   (&huart2)        /* TODO(移植) */
#define BSP_UART_IRQn     USART2_IRQn      /* TODO(移植) */

/* -------------------------------------------------------------------------
 * RX 环形缓冲：单生产者（ISR）、单消费者（任务）
 * head 只被 ISR 修改，tail 只被任务修改，因此无需加锁 ——
 * 这是经典 SPSC 环形队列，不是"省掉同步"，而是"用所有权消除同步需求"。
 * ---------------------------------------------------------------------- */
#define BSP_RX_CAPACITY   (256U)           /* 2 的幂，取模用位与 */
#define BSP_RX_MASK       (BSP_RX_CAPACITY - 1U)

static volatile uint8_t  s_rx[BSP_RX_CAPACITY];
static volatile uint16_t s_rx_head;        /* 写入位置（ISR） */
static volatile uint16_t s_rx_tail;        /* 读取位置（任务） */

static TaskHandle_t      s_reader_task;    /* 等待数据的任务（CommandTask） */

/* =========================================================================
 * BSP_UART_Init
 * ====================================================================== */
void BSP_UART_Init(uint32_t baud)
{
    (void)baud;   /* 波特率由 CubeMX 生成的 MX_USARTx_UART_Init 设定；
                   * 这里不重复配置，避免出现"两个地方都能改波特率"的隐患。 */

    s_rx_head = 0U;
    s_rx_tail = 0U;

    /* 使能接收中断：每次收到 1 字节触发 USARTx_IRQHandler */
    (void)HAL_UART_Receive_IT(BSP_UART_HANDLE, (uint8_t *)&s_rx[s_rx_head], 1U);

    HAL_NVIC_SetPriority(BSP_UART_IRQn, 5U, 0U);   /* 见 docs 中断优先级表 */
    HAL_NVIC_EnableIRQ(BSP_UART_IRQn);
}

/* =========================================================================
 * ISR 回调：只做「入缓冲 + 通知」，绝不做解析
 *
 * 注意优先级约束：调用 vTaskNotifyGiveFromISR 的中断，其抢占优先级数值
 * 必须 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY（即优先级不能更高），
 * 否则 FreeRTOS 的 FromISR API 会破坏内核临界区。
 * ====================================================================== */
/* 这是 HAL 的弱回调，名字与签名必须完全一致，否则永远不会被调用 ——
 * 一个"看起来写了但从不执行"的回调，是这类代码最常见的失效方式。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (huart != BSP_UART_HANDLE) {
        return;   /* 工程里有多个串口时，只处理调试口 */
    }

    /* 1) 入环形缓冲 */
    s_rx_head = (uint16_t)((s_rx_head + 1U) & BSP_RX_MASK);

    /* 2) 重新武装下一字节接收 */
    (void)HAL_UART_Receive_IT(BSP_UART_HANDLE, (uint8_t *)&s_rx[s_rx_head], 1U);

    /* 3) 通知等待中的任务（不做任何解析、不 printf） */
    if (s_reader_task != 0) {
        vTaskNotifyGiveFromISR(s_reader_task, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/* =========================================================================
 * BSP_UART_Read
 *
 * 语义与主机仿真版完全一致（至少等到一个字节或超时），
 * 但阻塞机制不同：这里是任务通知，仿真里是 1 ms 轮询。
 * 差异被限制在 BSP 层内部，CommandTask 完全感知不到。
 * ====================================================================== */
uint32_t BSP_UART_Read(char *buf, uint32_t max_len, uint32_t timeout_ms)
{
    uint32_t got = 0U;

    if ((buf == 0) || (max_len == 0U)) {
        return 0U;
    }

    /* 登记等待者，让 ISR 知道该通知谁 */
    s_reader_task = xTaskGetCurrentTaskHandle();

    /* 先试着直接取；没有数据才进入阻塞 */
    while (s_rx_tail != s_rx_head) {
        buf[got] = (char)s_rx[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) & BSP_RX_MASK);
        ++got;
        if (got >= max_len) {
            return got;
        }
    }
    if (got > 0U) {
        return got;
    }

    /* 阻塞等待通知；超时返回 0，与仿真实现语义一致 */
    (void)ulTaskNotifyTake(pdTRUE, (TickType_t)pdMS_TO_TICKS(timeout_ms));

    while ((s_rx_tail != s_rx_head) && (got < max_len)) {
        buf[got] = (char)s_rx[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) & BSP_RX_MASK);
        ++got;
    }

    return got;
}

/* =========================================================================
 * 输出
 *
 * ICD §17：UART 发送必须避免长时间阻塞 ControlTask；V1.0 若用简单阻塞发送，
 * 也必须仅在 DebugTask 中执行。本实现用阻塞发送，并靠"只有 DebugTask 调用"
 * 这一条纪律来保证实时性 —— 注释写在这里，因为这里是唯一能破坏它的地方。
 * ====================================================================== */
void BSP_UART_WriteString(const char *text)
{
    if (text == 0) {
        return;
    }
    (void)HAL_UART_Transmit(BSP_UART_HANDLE,
                            (uint8_t *)text,
                            (uint16_t)strlen(text),
                            (uint32_t)RC_UART_TX_TIMEOUT_MS);
}

void BSP_UART_WriteLine(const char *text)
{
    BSP_UART_WriteString(text);
    BSP_UART_WriteString("\r\n");
}
