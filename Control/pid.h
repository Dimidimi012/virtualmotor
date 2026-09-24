/**
 * @file    pid.h
 * @brief   PID 控制器接口 —— 对应 ICD V1.0 第 6 节「PID 接口定义」（冻结）
 *
 * 边界（ICD §2 / §23）：
 *   - 只负责控制算法状态与计算，不知道 UART、FreeRTOS 或具体电机硬件；
 *   - 本模块只依赖 Common，禁止 include FreeRTOS.h / HAL / UART 头文件；
 *   - PIDController 不暴露给 Command、Debug、Monitor 修改（ICD §6）。
 *
 * 控制权（ICD §6，冻结）：
 *   PID_Update 只允许 Control Task 调用。
 *   参数调节 V1.0 仅在初始化阶段完成，运行中在线调参属于后续版本。
 *
 * 本模块可在 PC 上单独编译并做单元测试（ICD §28 阶段 A）。
 */

#ifndef RC_CONTROL_PID_H
#define RC_CONTROL_PID_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * PIDController —— ICD §6 冻结结构体
 * 字段顺序与命名必须与 ICD 完全一致，任何改动都必须走 ICD Change Request。
 * ====================================================================== */
typedef struct {
    float kp;               /**< 比例增益，无量纲                     */
    float ki;               /**< 积分增益，单位：输出/(误差·s)        */
    float kd;               /**< 微分增益，单位：输出/(误差/s)        */
    float integral;         /**< 积分累加器，单位：误差·s             */
    float last_error;       /**< 上一拍误差，用于离散微分             */
    float output_limit;     /**< 输出限幅，与执行器量纲一致           */
    float integral_limit;   /**< 积分限幅，单位：误差·s（抗饱和）     */
} PIDController;

/* =========================================================================
 * 接口（ICD §6 冻结）
 * ====================================================================== */

/**
 * @brief 初始化 PID 控制器并清零内部状态
 *
 * @param pid             控制器对象（由调用者提供存储，本模块不做动态分配）
 * @param kp              比例增益
 * @param ki              积分增益，单位 输出/(误差·s)
 * @param kd              微分增益，单位 输出/(误差/s)
 * @param output_limit    输出限幅（取正值，内部对称使用）
 * @param integral_limit  积分限幅（取正值，内部对称使用）
 *
 * 只允许在初始化阶段调用（ICD §6 / §22 启动顺序：MotorManager_Init 之后、任务创建之前）。
 */
void PID_Init(PIDController *pid,
              float kp, float ki, float kd,
              float output_limit,
              float integral_limit);

/**
 * @brief 执行一拍 PID 计算
 *
 * 算法严格遵循 ICD §7「PID V1.0 算法冻结规则」：
 *     error      = target - actual
 *     integral  += error * dt
 *     derivative = (error - last_error) / dt
 *     output     = kp*error + ki*integral + kd*derivative
 *     integral   = clamp(integral, -integral_limit, +integral_limit)
 *     output     = clamp(output,   -output_limit,   +output_limit)
 *     last_error = error
 *
 * dt <= 0 的处理（ICD §7「若 dt <= 0，PID_Update 必须安全返回并避免除零」、
 * ICD §21「PID 非法 dt → 安全退出」）：
 *     不修改任何内部状态，直接返回 0.0f。
 *     理由：dt <= 0 在 V1.0 中只可能来自编码错误（ControlTask 固定传 0.01f），
 *           此时对执行器最安全的指令是零输出（fail-safe），
 *           且保留状态可避免把一次配置错误永久污染积分器。
 *
 * @param pid     控制器对象
 * @param target  目标值
 * @param actual  实际值
 * @param dt      控制周期，单位秒
 * @return 限幅后的控制输出；dt <= 0 时返回 0.0f
 *
 * @warning 只允许 Control Task 调用（ICD §6 / §18 规则 1）。
 */
float PID_Update(PIDController *pid,
                 float target,
                 float actual,
                 float dt);

/**
 * @brief 清零积分器与历史误差（不清零增益与限幅）
 *
 * ICD §27：RESET 命令需要执行 PID_Reset。
 */
void PID_Reset(PIDController *pid);

#ifdef __cplusplus
}
#endif

#endif /* RC_CONTROL_PID_H */
