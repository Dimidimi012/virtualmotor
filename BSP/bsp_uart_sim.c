/**
 * @file    bsp_uart_sim.c
 * @brief   主机仿真串口实现
 *
 * 设计取舍：
 *   真实硬件上 BSP_UART_Read 会阻塞在信号量/队列上，由 UART 中断唤醒；
 *   主机仿真里没有中断，因此用「1 ms 粒度的 vTaskDelay 轮询」模拟阻塞。
 *   代价是仿真中 CommandTask 的响应延迟最多 1 ms —— 相对 10 ms 控制周期可忽略，
 *   而换来的是仿真完全不依赖宿主线程与信号，结果逐拍可复现。
 */

#include "bsp_uart.h"
#include "bsp_uart_sim.h"

#include "task.h"          /* 仿真层提供，用于 vTaskDelay / xTaskGetTickCount */
#include "sim_rtos.h"

#include <stdio.h>
#include <string.h>

#define BSP_SIM_RX_CAPACITY   (512U)
#define BSP_SIM_OUT_CAPACITY  (256U * 1024U)
#define BSP_SIM_SCRIPT_MAX    (32U)

/* -------------------------------------------------------------------------
 * 内部状态
 * ---------------------------------------------------------------------- */
static char     s_rx[BSP_SIM_RX_CAPACITY];
static uint32_t s_rx_head;
static uint32_t s_rx_count;

static char     s_out[BSP_SIM_OUT_CAPACITY];
static uint32_t s_out_len;

static BSP_UART_SimEntry s_script[BSP_SIM_SCRIPT_MAX];
static uint32_t          s_script_count;
static bool              s_echo = true;
static bool              s_script_echo;
static bool              s_initialised;

/* -------------------------------------------------------------------------
 * 内部辅助
 * ---------------------------------------------------------------------- */
static void bsp_sim_push_char(char c)
{
    if (s_rx_count >= BSP_SIM_RX_CAPACITY) {
        return;   /* 溢出即丢弃：仿真中不应发生，真机上由硬件 FIFO/环形缓冲承担 */
    }
    s_rx[(s_rx_head + s_rx_count) % BSP_SIM_RX_CAPACITY] = c;
    ++s_rx_count;
}

static void bsp_sim_push_text(const char *text)
{
    if (text == 0) {
        return;
    }
    while (*text != '\0') {
        bsp_sim_push_char(*text);
        ++text;
    }
    bsp_sim_push_char('\n');
}

/* 前向声明：回显要用到输出缓冲 */
static void bsp_sim_append_output(const char *text);

/** @brief 把到期的脚本项注入 RX FIFO */
static void bsp_sim_service_script(void)
{
    const uint32_t now = (uint32_t)sim_rtos_get_tick();
    uint32_t i;

    for (i = 0U; i < s_script_count; ++i) {
        if ((s_script[i].line != 0) && (s_script[i].tick <= now)) {
            if (s_script_echo) {
                /* 模拟"上位机把命令打到终端上"的效果，便于事后读日志 */
                bsp_sim_append_output("> ");
                bsp_sim_append_output(s_script[i].line);
                bsp_sim_append_output("\n");
            }
            bsp_sim_push_text(s_script[i].line);
            s_script[i].line = 0;      /* 只注入一次 */
        }
    }
}

static bool bsp_sim_pop_char(char *c)
{
    if (s_rx_count == 0U) {
        return false;
    }
    *c = s_rx[s_rx_head];
    s_rx_head = (s_rx_head + 1U) % BSP_SIM_RX_CAPACITY;
    --s_rx_count;
    return true;
}

static void bsp_sim_append_output(const char *text)
{
    const size_t len = strlen(text);
    size_t i;

    for (i = 0U; i < len; ++i) {
        if (s_out_len >= (BSP_SIM_OUT_CAPACITY - 1U)) {
            break;                     /* 捕获缓冲满：截断但保证 '\0' 结尾 */
        }
        s_out[s_out_len] = text[i];
        ++s_out_len;
    }
    s_out[s_out_len] = '\0';
}

/* =========================================================================
 * bsp_uart.h 的实现
 * ====================================================================== */
void BSP_UART_Init(uint32_t baud)
{
    (void)baud;   /* 仿真中没有真实波特率 */
    s_initialised = true;
    s_out[0] = '\0';
}

uint32_t BSP_UART_Read(char *buf, uint32_t max_len, uint32_t timeout_ms)
{
    uint32_t got = 0U;
    uint32_t waited = 0U;
    char     c;

    if ((buf == 0) || (max_len == 0U)) {
        return 0U;
    }

    bsp_sim_service_script();

    /* 先尽力取一个字节；取不到才进入「等待 + 超时」循环 */
    for (;;) {
        while ((got < max_len) && bsp_sim_pop_char(&c)) {
            buf[got] = c;
            ++got;
        }

        if (got > 0U) {
            break;
        }
        if (waited >= timeout_ms) {
            break;
        }

        /* 1 ms 粒度模拟阻塞等待；同时让出 CPU 给高优先级任务 */
        vTaskDelay(1U);
        ++waited;
        bsp_sim_service_script();
    }

    return got;
}

void BSP_UART_WriteString(const char *text)
{
    if (text == 0) {
        return;
    }

    bsp_sim_append_output(text);
    if (s_echo) {
        fputs(text, stdout);
    }
}

void BSP_UART_WriteLine(const char *text)
{
    BSP_UART_WriteString(text);
    BSP_UART_WriteString("\n");
}

/* =========================================================================
 * 仿真扩展
 * ====================================================================== */
void BSP_UART_Sim_Reset(void)
{
    s_rx_head      = 0U;
    s_rx_count     = 0U;
    s_out_len      = 0U;
    s_out[0]       = '\0';
    s_script_count = 0U;
    s_echo         = true;
    s_script_echo  = false;
    s_initialised  = false;
    memset(s_script, 0, sizeof(s_script));
}

void BSP_UART_Sim_Inject(const char *line)
{
    bsp_sim_push_text(line);
}

void BSP_UART_Sim_LoadScript(const BSP_UART_SimEntry *entries, uint32_t count)
{
    uint32_t i;

    if ((entries == 0) || (count > BSP_SIM_SCRIPT_MAX)) {
        return;
    }

    s_script_count = count;
    for (i = 0U; i < count; ++i) {
        s_script[i] = entries[i];
    }
}

void BSP_UART_Sim_SetEcho(bool enabled)
{
    s_echo = enabled;
}

void BSP_UART_Sim_SetScriptEcho(bool enabled)
{
    s_script_echo = enabled;
}

const char *BSP_UART_Sim_GetOutput(void)
{
    return s_out;
}

void BSP_UART_Sim_ClearOutput(void)
{
    s_out_len = 0U;
    s_out[0]  = '\0';
}

uint32_t BSP_UART_Sim_GetOutputLength(void)
{
    return s_out_len;
}
