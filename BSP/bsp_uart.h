/**
 * @file    bsp_uart.h
 * @brief   板级串口接口 —— 对应 ICD V1.0 第 24 节 BSP 层
 *
 * 边界（ICD §2）：
 *   BSP 只提供 UART、Timer 等板级接口，不含任何业务逻辑；
 *   它不知道 CommandMessage，也不知道 MotorManager。
 *
 * 两个实现：
 *   BSP/bsp_uart_sim.c    —— 主机仿真（脚本化激励 + 标准输出）
 *   BSP/bsp_uart_stm32.c  —— STM32 HAL（Hardware Mode，V1.0 预留）
 *
 * ICD §11：UART ISR 不允许执行复杂解析和 printf，ISR 只接收字节并通知任务。
 *          因此本接口的读函数是「阻塞到有数据或超时」，由任务上下文调用，
 *          具体实现决定是轮询、DMA 还是信号量唤醒。
 */

#ifndef RC_BSP_UART_H
#define RC_BSP_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 初始化串口 */
void BSP_UART_Init(uint32_t baud);

/**
 * @brief 读取至多 max_len 个字节，至少等到一个字节或超时
 *
 * @param buf        接收缓冲区
 * @param max_len    缓冲区容量
 * @param timeout_ms 超时（毫秒）
 * @return 实际读到的字节数；0 表示超时且无数据
 * @note 只在任务上下文调用。禁止在 ISR 中调用。
 */
uint32_t BSP_UART_Read(char *buf, uint32_t max_len, uint32_t timeout_ms);

/** @brief 输出字符串（不带换行）。ICD §17：只允许 DebugTask 调用。 */
void BSP_UART_WriteString(const char *text);

/** @brief 输出字符串并换行 */
void BSP_UART_WriteLine(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* RC_BSP_UART_H */
