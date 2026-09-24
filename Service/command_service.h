/**
 * @file    command_service.h
 * @brief   命令服务 —— 对应 ICD V1.0 第 11 节「Command Service API」（冻结）
 *
 * 职责（ICD §2）：命令解析、命令合法性检查、发送消息。
 *
 * 职责分离（ICD §11，冻结）：
 *   Parse  只解析；
 *   Send   只发送 Queue；
 *   Handle 只执行命令对应的系统操作。
 *
 * ICD §11 同时规定：UART ISR 不允许执行复杂解析和 printf，ISR 只接收字节并通知任务。
 * 因此本模块的所有函数都只在任务上下文被调用。
 *
 * 依赖（ICD §23）：Common + MotorManager（+ Common 层的移植抽象 rc_port.h）。
 * 不 include FreeRTOS.h。
 */

#ifndef RC_SERVICE_COMMAND_SERVICE_H
#define RC_SERVICE_COMMAND_SERVICE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * ICD §11 冻结接口
 * ====================================================================== */

/**
 * @brief 把一行文本命令解析成 CommandMessage
 *
 * 支持的命令（ICD §15 推荐集合，大小写不敏感、允许多余空白）：
 *     START
 *     STOP
 *     SET_SPEED <value>
 *     RESET
 *     STATUS
 *
 * @param text 以 '\0' 结尾的命令行（可含结尾的 CR/LF，会自动跳过）
 * @param out  解析结果；仅在返回 RC_OK 时有效
 * @return RC_OK 解析成功；RC_INVALID_PARAM 未知命令 / 参数缺失 / 参数非法
 * @note timestamp 字段在解析成功时由本函数通过 RC_Port_GetTick() 打戳。
 */
RC_Result CommandService_Parse(const char *text, CommandMessage *out);

/**
 * @brief 把消息发送到 CommandQueue（ICD §5 冻结格式，只传值）
 *
 * ICD §15：Queue 满时不得无限阻塞，应记录错误或丢弃非关键重复命令。
 * 本实现采用「不阻塞 + 计数」策略：立即返回 RC_BUSY，并累加被拒计数，
 * 由 MonitorTask 读取计数并置 RC_FAULT_QUEUE_FULL。
 *
 * @return RC_OK 成功；RC_BUSY 队列满
 */
RC_Result CommandService_Send(const CommandMessage *msg);

/**
 * @brief 执行命令对应的系统操作
 *
 * ICD §13：本函数在 ControlTask 上下文中被调用
 * （ControlTask 负责「非阻塞读取 CommandQueue」与「根据命令更新目标/状态」）。
 *
 * 因此这里只允许做「快、无阻塞、无 printf」的动作：
 *     CMD_START     -> MotorManager_SetState(MOTOR_RUNNING)
 *     CMD_STOP      -> MotorManager_SetState(MOTOR_STOPPED)
 *     CMD_SET_SPEED -> MotorManager_SetTargetSpeed(value)
 *     CMD_RESET     -> MotorManager_RequestReset() + 置 PID 复位请求
 *     CMD_STATUS    -> 置「立即打印一次状态」请求
 *
 * 说明：START 在 INIT 状态下会被 §10 转换表拒绝（INIT -> RUNNING 非法），
 *       这是冻结行为，不是缺陷；系统启动后由 main 置为 READY（ICD §27）。
 */
void CommandService_Handle(const CommandMessage *msg);

/* =========================================================================
 * V1.0 扩展接口（见 docs/ICD_CHANGE_REQUEST.md CR-004）
 * ====================================================================== */

/** @brief 初始化本模块的内部标志与统计（由 main 在创建任务前调用） */
void CommandService_Init(void);

/**
 * @brief 由 ControlTask 取走「PID 复位请求」（读后清零）
 * @return true 表示需要执行 PID_Reset()
 *
 * 原因：ICD §6 规定 PIDController 不暴露给 Command 模块修改，
 *       所以 CommandService 只能「请求」，真正的 PID_Reset 由持有者 ControlTask 执行。
 */
bool CommandService_TakePidResetRequest(void);

/**
 * @brief 由 DebugTask 取走「立即打印状态请求」（读后清零）
 * @return true 表示本次循环应立刻输出一行状态
 *
 * 原因：ICD §13 禁止 ControlTask 执行 printf，打印只能由 DebugTask 完成。
 */
bool CommandService_TakeStatusRequest(void);

/** @brief 读取因队列满而被拒绝的命令条数（由 MonitorTask 轮询） */
uint32_t CommandService_GetSendRejectCount(void);

/**
 * @brief 读取解析失败的命令条数（由 MonitorTask 轮询）
 *
 * 存在的理由：RC_FAULT_CMD_PARSE 这个故障位如果没有对应的计数来源就是死代码。
 * 现场最有价值的信息往往不是「有故障」，而是「哪一类故障、多少次」。
 */
uint32_t CommandService_GetParseRejectCount(void);

#ifdef __cplusplus
}
#endif

#endif /* RC_SERVICE_COMMAND_SERVICE_H */
