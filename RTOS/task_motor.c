/**
 * @file    task_motor.c
 * @brief   MotorTask 实现 —— 对应 ICD V1.0 第 14 节
 *
 * ICD §14 循环：
 *   1. vTaskDelayUntil 保证 10ms
 *   2. 获取 control_output
 *   3. 获取当前 state
 *   4. RUNNING：VirtualMotor_SetInput
 *   5. STOPPED/ERROR：输入置 0
 *   6. VirtualMotor_Update
 *   7. MotorManager_UpdateFeedback(speed, position)
 *   8. 更新电机心跳
 *
 * 关于第 8 条：本工程的「电机心跳」就是 MotorData.update_tick，
 * 由 MotorManager_UpdateFeedback 在第 7 步统一打戳（ICD §4）。
 * 单独再维护一个心跳变量只会制造第二个真相来源，故不引入。
 */

#include "task_motor.h"

#include "virtual_motor.h"
#include "motor_manager.h"
#include "config.h"

/* =========================================================================
 * 虚拟电机的唯一实例（ICD §18 规则 2）
 * ====================================================================== */
static VirtualMotor s_motor;

/* =========================================================================
 * TaskMotor_InitModel
 * ====================================================================== */
void TaskMotor_InitModel(void)
{
    VirtualMotor_Init(&s_motor);   /* 同时承担 RESET 时的复位职责（ICD §27） */
}

/* =========================================================================
 * TaskMotor_Run
 * ====================================================================== */
void TaskMotor_Run(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)argument;

    for (;;) {
        float      control_output;
        MotorState state;

        /* ---- 1. 固定 10 ms 周期（ICD §14 第 1 条） -------------------- */
        vTaskDelayUntil(&last_wake, (TickType_t)pdMS_TO_TICKS(RC_MOTOR_PERIOD_MS));

        /* ---- RESET 协调（CR-003）：VirtualMotor 只能由本任务写 ---------
         * ControlTask 在处理 CMD_RESET 时置位请求，这里取走并真正复位，
         * 从而满足 ICD §18 规则 2 与 §27「RESET -> VirtualMotor 重置」。 */
        if (MotorManager_TakeResetRequest()) {
            VirtualMotor_Init(&s_motor);
        }

        /* ---- 2/3. 读取控制输出与状态 --------------------------------- */
        control_output = MotorManager_GetControlOutput();
        state          = MotorManager_GetState();

        /* ---- 4/5. 只有 RUNNING 才给输入，其余情况输入置 0 -------------
         * 这是「执行器安全」的第二道闸门：第一道在 MotorManager
         * （非 RUNNING 时拒绝存储非零输出），这里再确保模型输入确实为 0。 */
        if (state == MOTOR_RUNNING) {
            VirtualMotor_SetInput(&s_motor, control_output);
        } else {
            VirtualMotor_SetInput(&s_motor, 0.0f);
        }

        /* ---- 6. 推进对象模型（ICD §8 冻结动力学） --------------------- */
        VirtualMotor_Update(&s_motor, RC_DEFAULT_DT);

        /* ---- 7/8. 回写反馈并打心跳时间戳（ICD §4 update_tick） -------- */
        MotorManager_UpdateFeedback(VirtualMotor_GetSpeed(&s_motor),
                                    VirtualMotor_GetPosition(&s_motor));
    }
}
