/**
 * @file    pid.c
 * @brief   PID 控制器实现 —— 对应 ICD V1.0 第 7 节「PID V1.0 算法冻结规则」
 *
 * 本文件是「顺序敏感」的实现：每一步的计算顺序都与 ICD §7 中列出的顺序一一对应，
 * 顺序不是风格问题而是语义问题（见 PID_Update 内的逐条编号注释）。
 *
 * 依赖：仅 types.h。无 FreeRTOS、无 HAL、无 UART、无 libm（ICD §23）。
 */

#include "pid.h"

/* -------------------------------------------------------------------------
 * 内部辅助：对称限幅
 * 说明：写成 static inline 而不是宏，保证类型安全；编译器会内联，无额外开销。
 * ---------------------------------------------------------------------- */
static inline float PID_ClampSymmetric(float value, float limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

/* =========================================================================
 * PID_Init
 * ====================================================================== */
void PID_Init(PIDController *pid,
              float kp, float ki, float kd,
              float output_limit,
              float integral_limit)
{
    if (pid == 0) {
        return;
    }

    pid->kp               = kp;
    pid->ki               = ki;
    pid->kd               = kd;
    pid->output_limit     = output_limit;
    pid->integral_limit   = integral_limit;

    /* 清零全部运行态，保证上电行为可复现（不依赖未初始化内存） */
    pid->integral   = 0.0f;
    pid->last_error = 0.0f;
}

/* =========================================================================
 * PID_Reset
 * ====================================================================== */
void PID_Reset(PIDController *pid)
{
    if (pid == 0) {
        return;
    }

    pid->integral   = 0.0f;
    pid->last_error = 0.0f;
}

/* =========================================================================
 * PID_Update —— 严格对应 ICD §7 的 7 行算法
 * ====================================================================== */
float PID_Update(PIDController *pid,
                 float target,
                 float actual,
                 float dt)
{
    float error;
    float derivative;
    float output;

    /* --- ICD §7 / §21：dt 非法时安全退出，不修改任何内部状态 ------------ */
    if (pid == 0) {
        return 0.0f;
    }
    if (dt <= 0.0f) {
        return 0.0f;
    }

    /* ① error = target - actual ---------------------------------------- */
    error = target - actual;

    /* ② integral += error * dt ----------------------------------------- */
    pid->integral += error * dt;

    /* ③ derivative = (error - last_error) / dt ------------------------- */
    derivative = (error - pid->last_error) / dt;

    /* ④ output = kp*error + ki*integral + kd*derivative ---------------- */
    output = (pid->kp * error)
           + (pid->ki * pid->integral)
           + (pid->kd * derivative);

    /* ⑤ integral = clamp(integral, -integral_limit, +integral_limit) --- */
    pid->integral = PID_ClampSymmetric(pid->integral, pid->integral_limit);

    /* ⑥ output = clamp(output, -output_limit, +output_limit) ----------- */
    output = PID_ClampSymmetric(output, pid->output_limit);

    /* ⑦ last_error = error -------------------------------------------- */
    pid->last_error = error;

    return output;
}
