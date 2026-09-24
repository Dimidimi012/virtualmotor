/**
 * @file    motor_manager.h
 * @brief   电机统一管理服务 —— 对应 ICD V1.0 第 4 / 9 / 10 节（冻结）
 *
 * 职责（ICD §2）：维护统一电机对象和状态机，对上提供电机 API。
 *
 * 数据所有权（ICD §4，最重要的冻结规则）：
 *   MotorManager 拥有 MotorData 的唯一存储。
 *   其他模块不得保存并长期修改 MotorData 指针。
 *   Control Task 通过快照读取反馈，并通过 API 提交控制输出。
 *   Motor Task 通过 API 更新反馈。
 *
 * 并发模型（ICD §18）：
 *   规则 4：共享 MotorData 必须通过 MotorManager API。
 *   规则 5：本模块被 ControlTask(10ms) 与 MotorTask(10ms) 同时访问，
 *           因此 GetSnapshot 采用「短临界区整块拷贝」，临界区内不调用阻塞 API。
 *   规则 6：禁止用 volatile 代替同步机制 —— 本模块未使用任何 volatile。
 *
 * 依赖（ICD §23）：仅 Common。不得 include FreeRTOS.h / HAL / UART。
 */

#ifndef RC_SERVICE_MOTOR_MANAGER_H
#define RC_SERVICE_MOTOR_MANAGER_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * ICD §9 核心冻结接口
 * ====================================================================== */

/**
 * @brief 初始化电机管理器：清零全部数据、状态置 MOTOR_INIT
 * @note 必须在创建任何任务之前调用（ICD §22 启动顺序）。
 */
void MotorManager_Init(void);

/**
 * @brief 设置目标速度
 * @param speed 目标速度，单位 mm/s
 * @return RC_OK；非有限值或 |speed| > RC_MAX_TARGET_SPEED 时返回 RC_INVALID_PARAM
 */
RC_Result MotorManager_SetTargetSpeed(float speed);

/** @brief 读取目标速度 */
float MotorManager_GetTargetSpeed(void);

/**
 * @brief 由 Control Task 提交本拍控制输出
 *
 * 安全不变式（本实现显式强制，见 docs/ICD_CHANGE_REQUEST.md CR-006）：
 *   当 state != MOTOR_RUNNING 时，输出被强制存为 0，
 *   保证「非运行态执行器一定得不到非零输出」这一条与具体任务实现无关地成立。
 *
 * @param output 控制输出
 * @return RC_OK；非有限值（NaN/Inf）时输出被置 0 并返回 RC_INVALID_PARAM
 */
RC_Result MotorManager_SetControlOutput(float output);

/** @brief 读取当前控制输出 */
float MotorManager_GetControlOutput(void);

/**
 * @brief 由 Motor Task 更新反馈
 * @param speed    实际速度
 * @param position 位置
 * @note update_tick 由本模块通过 RC_Port_GetTick() 自动打时间戳（ICD §4）。
 */
void MotorManager_UpdateFeedback(float speed, float position);

/** @brief 读取实际速度 */
float MotorManager_GetActualSpeed(void);

/** @brief 读取位置 */
float MotorManager_GetPosition(void);

/** @brief 读取当前状态 */
MotorState MotorManager_GetState(void);

/**
 * @brief 请求状态转换（ICD §10 转换表）
 *
 * 允许的转换（ICD §10 冻结）：
 *     INIT    -> READY, ERROR
 *     READY   -> RUNNING, STOPPED, ERROR
 *     RUNNING -> STOPPED, ERROR
 *     STOPPED -> RUNNING, READY, ERROR
 *     ERROR   -> INIT, READY（仅 RESET 后）
 * 禁止：RUNNING 直接跳 INIT；ERROR 直接 RUNNING；INIT 直接 RUNNING。
 * 非法转换返回 RC_INVALID_PARAM。
 *
 * 本实现的两条补充约定：
 *   1) from == to 视为幂等空操作，返回 RC_OK；
 *   2) ERROR -> READY 属于「仅 RESET 后」的路径，因此不接受来自 SetState，
 *      必须经 MotorManager_RequestReset()，返回 RC_INVALID_PARAM。
 *
 * 进入 MOTOR_ERROR 时，控制输出被强制清零。
 */
RC_Result MotorManager_SetState(MotorState next);

/**
 * @brief 取一份 MotorData 快照（值拷贝）
 *
 * ICD §9：GetSnapshot 返回值拷贝，而不是返回可修改的全局指针，
 * 以降低多任务共享数据误用风险。
 */
MotorData MotorManager_GetSnapshot(void);

/**
 * @brief 强制清零控制输出（ICD §9 / §16）
 * @note ICD §16 的用法：MonitorTask 判定严重错误时先 StopOutput，再由其置 ERROR。
 */
void MotorManager_StopOutput(void);

/* =========================================================================
 * V1.0 扩展接口（非 ICD §9 原列，见 docs/ICD_CHANGE_REQUEST.md CR-003）
 *
 * 背景：ICD §27 要求「RESET → PID_Reset + VirtualMotor 重置 + READY」，
 *       但 §9 冻结接口没有任何跨任务复位通道，而 VirtualMotor 只能由
 *       MotorTask 写入（§18 规则 2）。因此补一对最小协调接口。
 * ====================================================================== */

/**
 * @brief 请求一次完整复位（由 ControlTask 处理 CMD_RESET 时调用）
 *
 * 行为：
 *   1) 目标速度清零、控制输出清零；
 *   2) 沿 ICD §10 合法路径把状态归位到 MOTOR_READY
 *      （RUNNING/READY -> STOPPED -> READY；ERROR -> READY；INIT -> READY）；
 *   3) 置位内部复位请求标志，等待 MotorTask 取走并复位 VirtualMotor。
 *
 * @return RC_OK
 */
RC_Result MotorManager_RequestReset(void);

/**
 * @brief 由 MotorTask 取走复位请求（读后自动清零）
 * @return true 表示本次需要复位 VirtualMotor
 */
bool MotorManager_TakeResetRequest(void);

#ifdef __cplusplus
}
#endif

#endif /* RC_SERVICE_MOTOR_MANAGER_H */
