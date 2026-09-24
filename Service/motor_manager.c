/**
 * @file    motor_manager.c
 * @brief   电机统一管理服务实现 —— 对应 ICD V1.0 第 4 / 9 / 10 节
 *
 * 本文件是 MotorData 的「唯一存储点」（ICD §4）。
 * 全工程只有这里持有 s_motor，其他模块一律通过本文件暴露的 API 访问。
 */

#include "motor_manager.h"
#include "config.h"
#include "rc_port.h"

/* =========================================================================
 * 唯一存储（ICD §4：MotorManager 拥有 MotorData 的唯一存储）
 * static 修饰符本身就是「单一数据所有权」在代码层面的表达：
 * 其他编译单元即使声明 extern 也拿不到它。
 * ====================================================================== */
static MotorData s_motor;

/* 复位协调标志（CR-003）：ControlTask 置位，MotorTask 取走 */
static bool s_reset_request;

/* =========================================================================
 * 内部辅助
 * ====================================================================== */

/**
 * @brief ICD §10 状态机合法性判定
 *
 * @param allow_error_to_ready 仅 MotorManager_RequestReset 传 true，
 *        用来实现 §10 中「ERROR -> READY（仅 RESET 后）」这一条。
 */
static bool MotorManager_IsTransitionAllowed(MotorState from,
                                            MotorState to,
                                            bool allow_error_to_ready)
{
    if (from == to) {
        return true;   /* 幂等自转换 */
    }

    switch (from) {
        case MOTOR_INIT:
            return (to == MOTOR_READY) || (to == MOTOR_ERROR);

        case MOTOR_READY:
            return (to == MOTOR_RUNNING) || (to == MOTOR_STOPPED) || (to == MOTOR_ERROR);

        case MOTOR_RUNNING:
            /* ICD §10 明确禁止 RUNNING -> INIT */
            return (to == MOTOR_STOPPED) || (to == MOTOR_ERROR);

        case MOTOR_STOPPED:
            return (to == MOTOR_RUNNING) || (to == MOTOR_READY) || (to == MOTOR_ERROR);

        case MOTOR_ERROR:
            /* ICD §10：ERROR -> INIT, READY（仅 RESET 后）；禁止 ERROR -> RUNNING */
            if (to == MOTOR_INIT) {
                return true;
            }
            if (to == MOTOR_READY) {
                return allow_error_to_ready;
            }
            return false;

        default:
            return false;
    }
}

/**
 * @brief 内部状态设置（不做合法性检查，仅供本文件已校验过的路径使用）
 */
static void MotorManager_ForceState(MotorState next)
{
    s_motor.state = next;

    /* 进入 ERROR 时强制清零输出：故障态绝不能继续驱动执行器（ICD §16） */
    if (next == MOTOR_ERROR) {
        s_motor.control_output = 0.0f;
    }
}

/**
 * @brief 带「RESET 通道」开关的内部状态转换
 *
 * @param allow_error_to_ready 只有 MotorManager_RequestReset 传 true。
 *        这样 §10 的「ERROR -> READY（仅 RESET 后）」在代码里有唯一入口，
 *        既不是靠约定、也不是靠注释，而是靠参数控制的可达性。
 */
static RC_Result MotorManager_TrySetState(MotorState next, bool allow_error_to_ready)
{
    if (!MotorManager_IsTransitionAllowed(s_motor.state, next, allow_error_to_ready)) {
        /* ICD §10 / §21：非法状态转换返回 RC_INVALID_PARAM */
        return RC_INVALID_PARAM;
    }

    MotorManager_ForceState(next);
    return RC_OK;
}

/* =========================================================================
 * MotorManager_Init
 * ====================================================================== */
void MotorManager_Init(void)
{
    s_motor.target_speed   = 0.0f;
    s_motor.actual_speed   = 0.0f;
    s_motor.position       = 0.0f;
    s_motor.control_output = 0.0f;
    s_motor.update_tick    = 0U;
    s_motor.state          = MOTOR_INIT;

    s_reset_request = false;
}

/* =========================================================================
 * 目标速度
 * ====================================================================== */
RC_Result MotorManager_SetTargetSpeed(float speed)
{
    /* ICD §21：普通参数错误 -> RC_INVALID_PARAM
     * 先判非有限值，再判量程；NaN 与任何比较都是 false，顺序不能颠倒。 */
    if (!RC_IsFinite(speed)) {
        return RC_INVALID_PARAM;
    }
    if ((speed > RC_MAX_TARGET_SPEED) || (speed < -RC_MAX_TARGET_SPEED)) {
        return RC_INVALID_PARAM;
    }

    s_motor.target_speed = speed;
    return RC_OK;
}

float MotorManager_GetTargetSpeed(void)
{
    /* 单字长对齐访问在 ARMv7-M 上是原子的，无需临界区（ICD §18 规则 5 的隐含前提） */
    return s_motor.target_speed;
}

/* =========================================================================
 * 控制输出
 * ====================================================================== */
RC_Result MotorManager_SetControlOutput(float output)
{
    if (!RC_IsFinite(output)) {
        /* fail-safe：绝不让 NaN/Inf 进入执行器通路 */
        s_motor.control_output = 0.0f;
        return RC_INVALID_PARAM;
    }

    /* 安全不变式：非 RUNNING 状态一律不接受非零输出 */
    if (s_motor.state != MOTOR_RUNNING) {
        s_motor.control_output = 0.0f;
        return RC_OK;
    }

    if (output > RC_DEFAULT_OUTPUT_LIMIT) {
        output = RC_DEFAULT_OUTPUT_LIMIT;
    } else if (output < -RC_DEFAULT_OUTPUT_LIMIT) {
        output = -RC_DEFAULT_OUTPUT_LIMIT;
    }

    s_motor.control_output = output;
    return RC_OK;
}

float MotorManager_GetControlOutput(void)
{
    return s_motor.control_output;
}

void MotorManager_StopOutput(void)
{
    s_motor.control_output = 0.0f;
}

/* =========================================================================
 * 反馈更新（MotorTask -> MotorManager）
 * ====================================================================== */
void MotorManager_UpdateFeedback(float speed, float position)
{
    s_motor.actual_speed = speed;
    s_motor.position     = position;
    s_motor.update_tick  = RC_Port_GetTick();
}

float MotorManager_GetActualSpeed(void)
{
    return s_motor.actual_speed;
}

float MotorManager_GetPosition(void)
{
    return s_motor.position;
}

MotorState MotorManager_GetState(void)
{
    return s_motor.state;
}

/* =========================================================================
 * 状态机
 * ====================================================================== */
RC_Result MotorManager_SetState(MotorState next)
{
    /* 对外接口永远关闭 RESET 通道：ERROR -> READY 只能经 RequestReset */
    return MotorManager_TrySetState(next, false);
}

/* =========================================================================
 * 快照（ICD §9 + §18 规则 5）
 * ====================================================================== */
MotorData MotorManager_GetSnapshot(void)
{
    MotorData snapshot;

    /* 6 个字段的整块拷贝不是原子操作，需要短临界区；
     * 临界区内只有赋值，没有任何阻塞 API（ICD §18 规则 5）。 */
    RC_Port_EnterCritical();
    snapshot = s_motor;
    RC_Port_ExitCritical();

    return snapshot;
}

/* =========================================================================
 * 复位协调（CR-003）
 * ====================================================================== */
RC_Result MotorManager_RequestReset(void)
{
    RC_Result rc = RC_OK;

    /* 1) 数据归零 */
    s_motor.target_speed   = 0.0f;
    s_motor.control_output = 0.0f;

    /* 2) 沿 ICD §10 合法路径归位到 READY，绝不跳转 */
    switch (s_motor.state) {
        case MOTOR_ERROR:
            /* §10：ERROR -> READY（仅 RESET 后）—— 全工程唯一的 RESET 通道入口 */
            rc = MotorManager_TrySetState(MOTOR_READY, true);
            break;

        case MOTOR_INIT:
            rc = MotorManager_TrySetState(MOTOR_READY, true);
            break;

        case MOTOR_RUNNING:
            /* 走 §10 的合法路径 RUNNING -> STOPPED -> READY，不跳转 */
            rc = MotorManager_TrySetState(MOTOR_STOPPED, true);
            if (rc == RC_OK) {
                rc = MotorManager_TrySetState(MOTOR_READY, true);
            }
            break;

        case MOTOR_READY:
        case MOTOR_STOPPED:
            /* READY -> READY 是幂等自转换；STOPPED -> READY 是 §10 允许的转换。
             * 不在这里吞掉错误码：任何异常都必须向上暴露，否则故障会被静默。 */
            rc = MotorManager_TrySetState(MOTOR_READY, true);
            break;

        default:
            rc = RC_ERROR;
            break;
    }

    /* 3) 通知 MotorTask 复位虚拟电机（§18 规则 2：VirtualMotor 只能由 MotorTask 写） */
    s_reset_request = true;

    /* 复位时也要清掉复位前可能残留的反馈时间戳语义：不修改 update_tick，
     * 让 MonitorTask 的「反馈过期」判定继续基于真实时间。 */

    return rc;
}

bool MotorManager_TakeResetRequest(void)
{
    bool requested = s_reset_request;
    s_reset_request = false;
    return requested;
}
