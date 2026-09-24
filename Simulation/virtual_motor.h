/**
 * @file    virtual_motor.h
 * @brief   虚拟电机模型接口 —— 对应 ICD V1.0 第 8 节「Virtual Motor 接口定义」（冻结）
 *
 * 边界（ICD §2 / §23）：
 *   - 只负责电机动力学模拟与反馈生成；
 *   - 不依赖 FreeRTOS、不依赖 UART、不访问 MotorManager；
 *   - 写入权：仅 MotorTask（ICD §18 规则 2）。
 *
 * ICD §8 明确：V1.0 模型是「演示级一阶简化动力学模型」，
 * 不是完整电机电磁模型。展示时应主动说明这一点 —— 这体现工程边界意识。
 *
 * 本模块可在 PC 上单独编译并做单元测试（ICD §28 阶段 A）。
 */

#ifndef RC_SIMULATION_VIRTUAL_MOTOR_H
#define RC_SIMULATION_VIRTUAL_MOTOR_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * VirtualMotor —— ICD §8 冻结结构体（字段顺序与命名不得改动）
 * ====================================================================== */
typedef struct {
    float speed;       /**< 当前转速（模型输出）                      */
    float position;    /**< 累积位置（速度的积分）                    */
    float input;       /**< 控制输入，由 MotorTask 写入               */
    float motor_gain;  /**< 增益，单位 速度·s^-1 / 输入单位（对应 Kt/J） */
    float damping;     /**< 阻尼，单位 s^-1（对应 B/J），其倒数为 tau */
    float load;        /**< 恒定负载扰动，单位 速度·s^-1              */
} VirtualMotor;

/* =========================================================================
 * 接口（ICD §8 冻结）
 * ====================================================================== */

/**
 * @brief 初始化（同时用于复位）虚拟电机
 *
 * ICD §8 的签名不带参数，因此模型参数取自 Common/config.h 的
 * RC_VMOTOR_GAIN / RC_VMOTOR_DAMPING / RC_VMOTOR_LOAD。
 * 同时把 speed / position / input 全部清零，保证上电与 RESET 行为可复现。
 */
void VirtualMotor_Init(VirtualMotor *motor);

/** @brief 设置控制输入（只允许 MotorTask 调用，ICD §18 规则 2） */
void VirtualMotor_SetInput(VirtualMotor *motor, float input);

/** @brief 设置恒定负载扰动（V1.0 用于演示「带载」与负载扰动场景） */
void VirtualMotor_SetLoad(VirtualMotor *motor, float load);

/**
 * @brief 推进一个仿真步
 *
 * ICD §8 冻结模型：
 *     acceleration = motor_gain * input - damping * speed - load
 *     speed       += acceleration * dt
 *     position    += speed * dt
 *
 * 注意顺序：先更新 speed，再用「新的 speed」积分 position。
 * 这是半隐式（semi-implicit）欧拉法，比显式欧拉稳定，
 * 也是 ICD 原文写定的顺序，实现必须一致。
 *
 * @param motor 电机对象
 * @param dt    仿真步长（秒）。dt <= 0 时不推进，直接返回（避免状态被污染）。
 */
void VirtualMotor_Update(VirtualMotor *motor, float dt);

/** @brief 读取转速（模型输出） */
float VirtualMotor_GetSpeed(const VirtualMotor *motor);

/** @brief 读取位置 */
float VirtualMotor_GetPosition(const VirtualMotor *motor);

#ifdef __cplusplus
}
#endif

#endif /* RC_SIMULATION_VIRTUAL_MOTOR_H */
