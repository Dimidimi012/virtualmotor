/**
 * @file    bsp_uart_sim.h
 * @brief   主机仿真串口的扩展接口（仅仿真构建可见）
 *
 * 这些函数不属于产品逻辑，只服务于：
 *   - 让仿真程序扮演「上位机」按脚本注入命令；
 *   - 让测试与演示捕获 UART 输出，而不必解析控制台。
 */

#ifndef RC_BSP_UART_SIM_H
#define RC_BSP_UART_SIM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 一条脚本化激励：在 tick 时刻向串口注入一行文本 */
typedef struct {
    uint32_t    tick;
    const char *line;
} BSP_UART_SimEntry;

/** @brief 清空 FIFO / 脚本 / 捕获缓冲（测试用例之间复用） */
void BSP_UART_Sim_Reset(void);

/** @brief 立即注入一行文本（自动补 '\n'） */
void BSP_UART_Sim_Inject(const char *line);

/** @brief 装载脚本化激励表（按 tick 顺序，允许乱序，内部每次检查到期项） */
void BSP_UART_Sim_LoadScript(const BSP_UART_SimEntry *entries, uint32_t count);

/** @brief 是否把 UART 输出同时打到标准输出 */
void BSP_UART_Sim_SetEcho(bool enabled);

/**
 * @brief 是否把脚本注入的命令回显到输出（前缀 "> "）
 *
 * 打开后，仿真控制台的记录读起来就是一次真实的串口会话；
 * 测试用例默认关闭，以免污染对输出内容的断言。
 */
void BSP_UART_Sim_SetScriptEcho(bool enabled);

/** @brief 取回捕获到的全部输出文本（'\0' 结尾） */
const char *BSP_UART_Sim_GetOutput(void);

/** @brief 清空捕获缓冲 */
void BSP_UART_Sim_ClearOutput(void);

/** @brief 当前捕获缓冲中的字节数 */
uint32_t BSP_UART_Sim_GetOutputLength(void);

#ifdef __cplusplus
}
#endif

#endif /* RC_BSP_UART_SIM_H */
