/**
 * @file    rc_port.h
 * @brief   RoboControl V1.0 平台移植接口（Port / 依赖倒置缝）
 *
 * 【为什么需要这个文件】
 * ICD §4 要求 MotorData.update_tick 记录「最近一次反馈更新时间」，
 * ICD §18 规则 5 又允许 MotorManager 用「短临界区复制快照」，
 * 但 ICD §23 同时规定 MotorManager 只允许依赖 Common，
 * 且禁止 include FreeRTOS.h。二者直接冲突。
 *
 * 解决办法是依赖倒置：由 Common 层「声明」平台需要提供的原语，
 * 由平台层（RTOS/ 任务层或 BSP/ 板级层）「实现」它。
 * 这样 MotorManager 只依赖 Common，却又能在真实 RTOS 上取得 tick 与临界区。
 *
 * 本文件只声明、不实现，也不包含任何平台头文件，因此仍属于 Common 层。
 *
 * 【由谁实现】
 *   - 主机仿真： RTOS/port_sim/port_sim.c
 *   - STM32 硬件：RTOS/port_freertos/port_freertos.c
 * 记录于 docs/ICD_CHANGE_REQUEST.md CR-005。
 */

#ifndef RC_COMMON_RC_PORT_H
#define RC_COMMON_RC_PORT_H

#include <stdint.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 取得单调递增的系统 tick（单位：ms，允许回绕）
 * @return 当前 tick 计数
 * @note 主机仿真由调度器推进；STM32 上直接返回 xTaskGetTickCount()。
 */
uint32_t RC_Port_GetTick(void);

/**
 * @brief 进入极短临界区
 * @note 临界区内禁止调用任何可能阻塞的 API（ICD §18 规则 5）。
 *       主机仿真是单线程确定性调度，实现为空函数。
 */
void RC_Port_EnterCritical(void);

/**
 * @brief 退出极短临界区，必须与 RC_Port_EnterCritical 成对出现
 */
void RC_Port_ExitCritical(void);

/* =========================================================================
 * 命令队列平台原语
 *
 * 【为什么也放在移植层】
 * ICD §11 冻结了 CommandService_Send(const CommandMessage *msg)，
 * 其语义就是「发送到 CommandQueue」；而 ICD §23 又规定
 * CommandService 只能依赖 Common + MotorManager，不得 include FreeRTOS.h。
 * 两者冲突，同样用依赖倒置解决：
 *   Common 声明「一个只传值的命令队列」这一抽象，
 *   平台层用 FreeRTOS Queue（硬件）或确定性队列（仿真）来实现。
 *
 * 接口语义按 ICD §5 / §15 / §21 冻结：
 *   - 只传值，不传裸指针；
 *   - 发送不阻塞：队列满时立即返回 RC_BUSY（ICD §15「不得无限阻塞」）。
 * ====================================================================== */

/** @brief 创建命令队列（必须在创建任何生产者/消费者任务之前调用，ICD §22） */
void RC_Port_CmdQueueCreate(void);

/**
 * @brief 非阻塞发送一条命令消息
 * @return RC_OK 成功；RC_BUSY 队列满（ICD §21）
 */
RC_Result RC_Port_CmdQueueSend(const CommandMessage *msg);

/**
 * @brief 非阻塞接收一条命令消息
 * @return RC_OK 取到消息；RC_TIMEOUT 队列为空
 */
RC_Result RC_Port_CmdQueueReceive(CommandMessage *msg);

#ifdef __cplusplus
}
#endif

#endif /* RC_COMMON_RC_PORT_H */
