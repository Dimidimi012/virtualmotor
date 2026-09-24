/**
 * @file    task_control.c
 * @brief   ControlTask 实现 —— 对应 ICD V1.0 第 13 节
 *
 * ICD §13 循环：
 *   1. vTaskDelayUntil 保证 10ms 周期
 *   2. 非阻塞读取 CommandQueue
 *   3. 根据命令更新目标/状态
 *   4. 获取 MotorData Snapshot
 *   5. 如果 state != RUNNING：输出 0
 *   6. 如果 RUNNING：PID_Update(target, actual, 0.01)
 *   7. MotorManager_SetControlOutput(output)
 *   8. 更新控制心跳
 *
 * ICD §13 禁止项（本文件严格遵守）：
 *   printf 大量日志、HAL_Delay、等待 UART、复杂字符串解析、直接修改 VirtualMotor 内部结构。
 */

#include "task_control.h"

#include "motor_manager.h"
#include "command_service.h"
#include "monitor_service.h"
#include "pid.h"
#include "config.h"
#include "rc_port.h"

/* =========================================================================
 * PID 的唯一实例（ICD §6：PIDController 不暴露给 Command、Debug、Monitor 修改）
 * static 保证外部连声明 extern 都拿不到，只能在下面那几个调用点被触碰。
 * ====================================================================== */
static PIDController s_pid;

/* =========================================================================
 * TaskControl_InitController
 * ====================================================================== */
void TaskControl_InitController(void)
{
    /* 整定参数集中来自 Common/config.h（ICD §20 禁止 Magic Number 散落）。
     * 量纲与推导过程写在 config.h 的 B5 段与 docs/tuning-log.md。 */
    PID_Init(&s_pid,
             RC_PID_KP, RC_PID_KI, RC_PID_KD,
             RC_PID_OUTPUT_LIMIT,
             RC_PID_INTEGRAL_LIMIT);
}

/* =========================================================================
 * TaskControl_Run
 * ====================================================================== */
void TaskControl_Run(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    CommandMessage msg;

    (void)argument;

    for (;;) {
        MotorData snapshot;
        float     output;
        bool      pid_needs_reset;

        /* ---- 1. 固定 10 ms 周期（ICD §13 第 1 条） -------------------- */
        vTaskDelayUntil(&last_wake, (TickType_t)pdMS_TO_TICKS(RC_CONTROL_PERIOD_MS));

        /* ---- 2/3. 非阻塞排空 CommandQueue 并执行命令（§13 第 2、3 条） --
         * 队列深度上限为 RC_CMD_QUEUE_LENGTH，因此最坏耗时有确定上界，
         * 不会让 10 ms 周期失控。 */
        while (RC_Port_CmdQueueReceive(&msg) == RC_OK) {
            CommandService_Handle(&msg);
        }

        /* ---- RESET 命令的落地部分：PID 由本任务持有，只能由本任务复位 ---
         * 同时清掉监控故障寄存器：RESET 的语义是「整套系统回到已知状态」。 */
        pid_needs_reset = CommandService_TakePidResetRequest();
        if (pid_needs_reset) {
            PID_Reset(&s_pid);
            MonitorService_ClearFaults();
        }

        /* ---- 4. 取反馈快照（ICD §4：通过 API 快照读取，不持有指针） ---- */
        snapshot = MotorManager_GetSnapshot();

        /* ---- 5/6. 只有 RUNNING 才跑 PID（ICD §13 第 5、6 条） ---------- */
        if (snapshot.state != MOTOR_RUNNING) {
            output = 0.0f;
        } else {
            output = PID_Update(&s_pid,
                                snapshot.target_speed,
                                snapshot.actual_speed,
                                RC_DEFAULT_DT);
        }

        /* ---- 7. 提交控制输出（ICD §13 第 7 条） ----------------------- */
        (void)MotorManager_SetControlOutput(output);

        /* ---- 8. 控制心跳（ICD §13 第 8 条） --------------------------- */
        MonitorService_NotifyControlHeartbeat();
    }
}
