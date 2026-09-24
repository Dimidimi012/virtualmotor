/**
 * @file    app.h
 * @brief   应用层：系统级启动流程与任务注册 —— 对应 ICD V1.0 第 22 节
 *
 * ICD §2：Application 负责系统级流程和启动逻辑。
 * ICD §22：初始化顺序由 main/Application 统一负责；
 *          任何 Task 不得假设其他模块"可能已经初始化"。
 *
 * 本模块存在的另一个理由：ICD §22 的启动顺序里同时出现 PID_Init 与
 * VirtualMotor_Init，但 ICD §6/§18 又要求 PID 只能由 ControlTask 触碰、
 * VirtualMotor 只能由 MotorTask 写入。与其把实例挪到 main 里破坏所有权，
 * 不如让 Application 调用各任务模块自己暴露的初始化函数 ——
 * 顺序不变，所有权也不变。
 */

#ifndef RC_APPLICATION_APP_H
#define RC_APPLICATION_APP_H

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 任务编号（与 App_GetTaskHandle 配套，供仿真/测试注入故障使用） */
typedef enum {
    APP_TASK_CONTROL = 0,
    APP_TASK_MOTOR,
    APP_TASK_COMMAND,
    APP_TASK_MONITOR,
    APP_TASK_DEBUG,
    APP_TASK_COUNT
} App_TaskId;

/**
 * @brief 执行 ICD §22 冻结的初始化顺序（不含 vTaskStartScheduler）
 *
 *   1) BSP Init
 *   2) MotorManager_Init
 *   3) PID_Init            （经 TaskControl_InitController）
 *   4) VirtualMotor_Init   （经 TaskMotor_InitModel）
 *   5) CommandQueue Create
 *   6) 服务层内部状态初始化
 *   7) 状态机 INIT -> READY（ICD §27「启动系统后 state = READY」）
 *
 * @return RC_OK 全部成功；否则返回第一个失败步骤的错误码（启动即失败，不带着未知状态运行）
 */
RC_Result App_Init(void);

/** @brief 按 ICD §12 的任务表创建全部任务 */
RC_Result App_CreateTasks(void);

/** @brief 取得任务句柄（未创建时返回 NULL）；供仿真注入故障使用 */
TaskHandle_t App_GetTaskHandle(App_TaskId id);

#ifdef __cplusplus
}
#endif

#endif /* RC_APPLICATION_APP_H */
