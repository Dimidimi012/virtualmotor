/**
 * @file    sim_rtos.h
 * @brief   主机仿真 RTOS 的「测试台」控制接口（仅主机仿真构建可见）
 *
 * 这些函数不参与产品逻辑，只服务于：
 *   - 让调度器在有限拍数后停下来，便于产出可复现的测试数据；
 *   - 让演示与测试程序能在仿真中扮演「外部世界」（UART 对端、故障注入）。
 */

#ifndef RC_PORT_SIM_SIM_RTOS_H
#define RC_PORT_SIM_SIM_RTOS_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 设置调度器的 tick 上限
 * @param ticks 达到该 tick 后 vTaskStartScheduler 返回；0 表示不限制
 */
void sim_rtos_set_tick_limit(TickType_t ticks);

/** @brief 读取当前 tick（与 xTaskGetTickCount 等价，语义更贴近测试台） */
TickType_t sim_rtos_get_tick(void);

/** @brief 重置调度器内部状态（清空任务表与 tick），供多个测试用例复用 */
void sim_rtos_reset(void);

/** @brief 当前是否处于任务上下文中 */
int sim_rtos_in_task(void);

/**
 * @brief 直接推进仿真时钟 n 毫秒（仅测试台使用）
 *
 * 用途：Monitor 的超时判定需要「时间流逝」，
 *       而单元测试里没有调度器在跑，必须能显式拨动时钟。
 */
void sim_rtos_advance_tick(TickType_t ticks);

#ifdef __cplusplus
}
#endif

#endif /* RC_PORT_SIM_SIM_RTOS_H */
