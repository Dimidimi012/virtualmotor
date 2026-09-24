/**
 * @file    monitor_service.c
 * @brief   监控服务实现 —— 对应 ICD V1.0 第 16 / 21 节
 *
 * 时间比较一律使用无符号回绕安全的写法： (now - then) > timeout
 * 直接写 now > then + timeout 在 tick 回绕时会失效，这是嵌入式里非常常见的隐性 bug。
 */

#include "monitor_service.h"
#include "motor_manager.h"
#include "config.h"
#include "rc_port.h"

/* -------------------------------------------------------------------------
 * 内部状态
 * ---------------------------------------------------------------------- */
static uint32_t s_faults;            /* 累积故障寄存器（按位或） */
static uint32_t s_last_control_hb;   /* ControlTask 最近一次心跳 tick */
static bool     s_noresp_active;     /* 「无响应」计时是否在跑 */
static uint32_t s_noresp_since;      /* 「无响应」计时起点 tick */
static uint32_t s_last_cmd_reject;      /* 上一次看到的队列溢出计数   */
static uint32_t s_last_cmd_parse_err;   /* 上一次看到的解析失败计数   */

/** @brief 绝对値（避免为 Service 层引入 libm） */
static float MonitorService_Abs(float x)
{
    return (x < 0.0f) ? -x : x;
}

/* =========================================================================
 * MonitorService_Init
 * ====================================================================== */
void MonitorService_Init(void)
{
    s_faults            = RC_FAULT_NONE;
    s_last_control_hb   = RC_Port_GetTick();
    s_noresp_active     = false;
    s_noresp_since      = 0U;
    s_last_cmd_reject   = 0U;
    s_last_cmd_parse_err = 0U;
}

/* =========================================================================
 * MonitorService_NotifyControlHeartbeat
 * ====================================================================== */
void MonitorService_NotifyControlHeartbeat(void)
{
    s_last_control_hb = RC_Port_GetTick();
}

/* =========================================================================
 * MonitorService_Check
 * ====================================================================== */
uint32_t MonitorService_Check(uint32_t cmd_queue_full_total,
                              uint32_t cmd_parse_error_total)
{
    const uint32_t now  = RC_Port_GetTick();
    const MotorData snap = MotorManager_GetSnapshot();
    uint32_t found = RC_FAULT_NONE;

    /* --- 1) Control 心跳（ICD §16 第 1 条） --------------------------------- */
    if ((now - s_last_control_hb) > (uint32_t)RC_HB_TIMEOUT_CONTROL_MS) {
        found |= RC_FAULT_CONTROL_HB;
    }

    /* --- 2) Motor 反馈过期（ICD §16 第 2 条） ------------------------------- */
    /* 注意：不能把「RESET 后从未有反馈」当成过期，因此以 update_tick 为基准；
     * 上电后若 MotorTask 从未跑过，update_tick 保持 0，now 一旦超过阈值即报故障，
     * 这正是 §27 故障注入「停止 Motor 更新 -> Monitor 最终 ERROR」期望的行为。 */
    if ((now - snap.update_tick) > (uint32_t)RC_HB_TIMEOUT_MOTOR_MS) {
        found |= RC_FAULT_MOTOR_FB;
    }

    /* --- 4) NaN / Inf（ICD §16 第 4 条） ------------------------------------ */
    if (!RC_IsFinite(snap.actual_speed) ||
        !RC_IsFinite(snap.control_output) ||
        !RC_IsFinite(snap.position) ||
        !RC_IsFinite(snap.target_speed)) {
        found |= RC_FAULT_NONFINITE;
    }

    /* --- 5) 超范围（ICD §16 第 4 条的后半句） ------------------------------- */
    if (MonitorService_Abs(snap.actual_speed) > RC_MONITOR_MAX_ABS_SPEED) {
        found |= RC_FAULT_OUT_OF_RANGE;
    }
    if (MonitorService_Abs(snap.control_output) > RC_MONITOR_MAX_ABS_OUTPUT) {
        found |= RC_FAULT_OUT_OF_RANGE;
    }

    /* --- 3) RUNNING 状态下长期无速度响应（ICD §16 第 3 条） ----------------- */
    if ((snap.state == MOTOR_RUNNING) &&
        (MonitorService_Abs(snap.target_speed) > RC_NORESPONSE_TARGET_MIN) &&
        (MonitorService_Abs(snap.actual_speed) < RC_NORESPONSE_SPEED_EPS)) {

        if (!s_noresp_active) {
            s_noresp_active = true;
            s_noresp_since  = now;
        } else if ((now - s_noresp_since) > (uint32_t)RC_NORESPONSE_TIMEOUT_MS) {
            found |= RC_FAULT_NO_RESPONSE;
        }
    } else {
        s_noresp_active = false;
    }

    /* --- 6) 命令队列溢出（ICD §21「Queue 满 -> 返回 RC_BUSY / 记录」） ------ */
    if (cmd_queue_full_total != s_last_cmd_reject) {
        s_last_cmd_reject = cmd_queue_full_total;
        found |= RC_FAULT_QUEUE_FULL;
    }

    /* --- 7) 命令解析失败 ---------------------------------------------------- */
    if (cmd_parse_error_total != s_last_cmd_parse_err) {
        s_last_cmd_parse_err = cmd_parse_error_total;
        found |= RC_FAULT_CMD_PARSE;
    }

    /* 累积到故障寄存器：一旦出现就保留，直到显式 RESET */
    s_faults |= found;

    return found;
}

/* =========================================================================
 * 故障寄存器读写
 * ====================================================================== */
uint32_t MonitorService_GetFaults(void)
{
    return s_faults;
}

void MonitorService_ClearFaults(void)
{
    s_faults             = RC_FAULT_NONE;
    s_last_cmd_reject    = 0U;
    s_last_cmd_parse_err = 0U;
    s_noresp_active      = false;
    s_noresp_since       = 0U;
    s_last_control_hb    = RC_Port_GetTick();
}
