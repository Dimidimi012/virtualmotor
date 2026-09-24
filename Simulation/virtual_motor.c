/**
 * @file    virtual_motor.c
 * @brief   虚拟电机模型实现 —— 对应 ICD V1.0 第 8 节
 *
 * 依赖：Common（types.h + config.h 提供默认模型参数）。
 * 无 FreeRTOS、无 HAL、无 UART、无 libm。
 */

#include "virtual_motor.h"
#include "config.h"

/* =========================================================================
 * VirtualMotor_Init
 * ====================================================================== */
void VirtualMotor_Init(VirtualMotor *motor)
{
    if (motor == 0) {
        return;
    }

    /* 模型参数来自集中配置（ICD §20：禁止 Magic Number 散落） */
    motor->motor_gain = RC_VMOTOR_GAIN;
    motor->damping    = RC_VMOTOR_DAMPING;
    motor->load       = RC_VMOTOR_LOAD;

    /* 运行态清零：保证上电 / RESET 后行为完全一致（ICD §27 RESET 语义） */
    motor->speed      = 0.0f;
    motor->position   = 0.0f;
    motor->input      = 0.0f;
}

/* =========================================================================
 * VirtualMotor_SetInput
 * ====================================================================== */
void VirtualMotor_SetInput(VirtualMotor *motor, float input)
{
    if (motor == 0) {
        return;
    }
    motor->input = input;
}

/* =========================================================================
 * VirtualMotor_SetLoad
 * ====================================================================== */
void VirtualMotor_SetLoad(VirtualMotor *motor, float load)
{
    if (motor == 0) {
        return;
    }
    motor->load = load;
}

/* =========================================================================
 * VirtualMotor_Update —— ICD §8 冻结动力学
 * ====================================================================== */
void VirtualMotor_Update(VirtualMotor *motor, float dt)
{
    float acceleration;

    if (motor == 0) {
        return;
    }
    if (dt <= 0.0f) {
        return;   /* 不推进，不用非法 dt 污染状态 */
    }

    /* acceleration = motor_gain * input - damping * speed - load */
    acceleration = (motor->motor_gain * motor->input)
                 - (motor->damping * motor->speed)
                 - motor->load;

    /* speed += acceleration * dt */
    motor->speed += acceleration * dt;

    /* position += speed * dt   —— 注意使用更新后的 speed（半隐式欧拉） */
    motor->position += motor->speed * dt;
}

/* =========================================================================
 * 只读访问器
 * ====================================================================== */
float VirtualMotor_GetSpeed(const VirtualMotor *motor)
{
    return (motor == 0) ? 0.0f : motor->speed;
}

float VirtualMotor_GetPosition(const VirtualMotor *motor)
{
    return (motor == 0) ? 0.0f : motor->position;
}
