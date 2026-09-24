/**
 * @file    test_monitor_service.c
 * @brief   Service/MonitorService 单元测试 —— ICD §16 基础保护 / §21 错误策略
 */

#include "test_framework.h"
#include "monitor_service.h"
#include "motor_manager.h"
#include "config.h"
#include "rc_port.h"
#include "sim_rtos.h"

/* 让心跳与反馈同时保持新鲜的辅助函数 */
static void keep_alive(void)
{
    MotorManager_UpdateFeedback(0.0f, 0.0f);
    MonitorService_NotifyControlHeartbeat();
}

void TestSuite_MonitorService(void)
{
    /* ------------------------------------------------------------------ */
    TC_BEGIN("健康系统：持续刷新心跳与反馈时不应产生任何故障（ICD §16）");
    {
        sim_rtos_reset();
        MotorManager_Init();
        MonitorService_Init();

        {
            uint32_t found = RC_FAULT_NONE;
            for (int i = 0; i < 50; ++i) {
                sim_rtos_advance_tick(10U);
                keep_alive();
                found |= MonitorService_Check(0U, 0U);
            }
            TC_CHECK(found == RC_FAULT_NONE);
            TC_CHECK(MonitorService_GetFaults() == RC_FAULT_NONE);
        }
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Control 心跳超时 -> RC_FAULT_CONTROL_HB（ICD §16 第 1 条）");
    {
        sim_rtos_reset();
        MotorManager_Init();
        MonitorService_Init();
        MonitorService_NotifyControlHeartbeat();

        sim_rtos_advance_tick((TickType_t)RC_HB_TIMEOUT_CONTROL_MS + 50U);
        MotorManager_UpdateFeedback(0.0f, 0.0f);      /* 反馈保持新鲜，隔离变量 */

        {
            const uint32_t found = MonitorService_Check(0U, 0U);
            TC_CHECK((found & RC_FAULT_CONTROL_HB) != 0U);
            TC_CHECK((found & RC_FAULT_MOTOR_FB) == 0U);
        }
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Motor 反馈过期 -> RC_FAULT_MOTOR_FB（ICD §16 第 2 条）");
    {
        sim_rtos_reset();
        MotorManager_Init();
        MonitorService_Init();

        sim_rtos_advance_tick((TickType_t)RC_HB_TIMEOUT_MOTOR_MS + 50U);
        MonitorService_NotifyControlHeartbeat();      /* 心跳保持新鲜，隔离变量 */

        {
            const uint32_t found = MonitorService_Check(0U, 0U);
            TC_CHECK((found & RC_FAULT_MOTOR_FB) != 0U);
            TC_CHECK((found & RC_FAULT_CONTROL_HB) == 0U);
        }
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("反馈出现 NaN -> RC_FAULT_NONFINITE（ICD §16 第 4 条）");
    {
        float inf = 1.0f;
        float nan;

        sim_rtos_reset();
        MotorManager_Init();
        MonitorService_Init();

        inf = inf / 0.0f;
        nan = inf - inf;

        sim_rtos_advance_tick(10U);
        MonitorService_NotifyControlHeartbeat();
        MotorManager_UpdateFeedback(nan, 0.0f);

        TC_CHECK((MonitorService_Check(0U, 0U) & RC_FAULT_NONFINITE) != 0U);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("速度超范围 -> RC_FAULT_OUT_OF_RANGE（ICD §16 第 4 条）");
    {
        sim_rtos_reset();
        MotorManager_Init();
        MonitorService_Init();

        sim_rtos_advance_tick(10U);
        MonitorService_NotifyControlHeartbeat();
        MotorManager_UpdateFeedback(RC_MONITOR_MAX_ABS_SPEED + 1.0f, 0.0f);

        TC_CHECK((MonitorService_Check(0U, 0U) & RC_FAULT_OUT_OF_RANGE) != 0U);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("命令队列曾溢出 -> RC_FAULT_QUEUE_FULL（ICD §21）");
    {
        sim_rtos_reset();
        MotorManager_Init();
        MonitorService_Init();
        sim_rtos_advance_tick(10U);
        keep_alive();

        TC_CHECK(MonitorService_Check(0U, 0U) == RC_FAULT_NONE);   /* 计数没变 -> 无故障 */
        TC_CHECK((MonitorService_Check(1U, 0U) & RC_FAULT_QUEUE_FULL) != 0U);  /* 计数变了 -> 报故障 */
        TC_CHECK(MonitorService_Check(1U, 0U) == RC_FAULT_NONE);   /* 同一计数不重复报 */
        TC_CHECK((MonitorService_Check(1U, 3U) & RC_FAULT_CMD_PARSE) != 0U);
        TC_CHECK(MonitorService_Check(1U, 3U) == RC_FAULT_NONE);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("RUNNING 且长期无速度响应 -> RC_FAULT_NO_RESPONSE（ICD §16 第 3 条）");
    {
        uint32_t found = RC_FAULT_NONE;

        sim_rtos_reset();
        MotorManager_Init();
        MonitorService_Init();
        (void)MotorManager_SetState(MOTOR_READY);
        (void)MotorManager_SetState(MOTOR_RUNNING);
        (void)MotorManager_SetTargetSpeed(1000.0f);

        /* 目标 1000 但实际速度恒为 0：跑满 3 秒 */
        for (int i = 0; i < 300; ++i) {
            sim_rtos_advance_tick(10U);
            keep_alive();
            found |= MonitorService_Check(0U, 0U);
        }
        TC_CHECK((found & RC_FAULT_NO_RESPONSE) != 0U);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("有正常响应时不得误报 NO_RESPONSE");
    {
        uint32_t found = RC_FAULT_NONE;

        sim_rtos_reset();
        MotorManager_Init();
        MonitorService_Init();
        (void)MotorManager_SetState(MOTOR_READY);
        (void)MotorManager_SetState(MOTOR_RUNNING);
        (void)MotorManager_SetTargetSpeed(1000.0f);

        for (int i = 0; i < 300; ++i) {
            sim_rtos_advance_tick(10U);
            MotorManager_UpdateFeedback(1000.0f, 0.0f);   /* 速度跟上了 */
            MonitorService_NotifyControlHeartbeat();
            found |= MonitorService_Check(0U, 0U);
        }
        TC_CHECK((found & RC_FAULT_NO_RESPONSE) == 0U);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("非 RUNNING 状态不得触发 NO_RESPONSE（ICD §16 只针对 RUNNING）");
    {
        uint32_t found = RC_FAULT_NONE;

        sim_rtos_reset();
        MotorManager_Init();
        MonitorService_Init();
        (void)MotorManager_SetState(MOTOR_READY);        /* 停在 READY */
        (void)MotorManager_SetTargetSpeed(1000.0f);

        for (int i = 0; i < 300; ++i) {
            sim_rtos_advance_tick(10U);
            keep_alive();
            found |= MonitorService_Check(0U, 0U);
        }
        TC_CHECK((found & RC_FAULT_NO_RESPONSE) == 0U);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("故障寄存器累积，ClearFaults 清零（ICD §27 RESET 语义）");
    {
        sim_rtos_reset();
        MotorManager_Init();
        MonitorService_Init();
        MonitorService_NotifyControlHeartbeat();

        sim_rtos_advance_tick((TickType_t)RC_HB_TIMEOUT_CONTROL_MS + 10U);
        MotorManager_UpdateFeedback(0.0f, 0.0f);
        (void)MonitorService_Check(0U, 0U);
        TC_CHECK(MonitorService_GetFaults() != RC_FAULT_NONE);

        /* 后续轮次即使恢复正常，历史故障位仍应保留 */
        keep_alive();
        (void)MonitorService_Check(0U, 0U);
        TC_CHECK(MonitorService_GetFaults() != RC_FAULT_NONE);

        MonitorService_ClearFaults();
        TC_CHECK(MonitorService_GetFaults() == RC_FAULT_NONE);
    }

    TC_SUITE_REPORT("MonitorService");
}
