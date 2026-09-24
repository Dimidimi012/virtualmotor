/**
 * @file    test_motor_manager.c
 * @brief   Service/MotorManager 单元测试 —— ICD §4 数据所有权 / §9 API / §10 状态机
 */

#include "test_framework.h"
#include "motor_manager.h"
#include "config.h"
#include "rc_port.h"
#include "sim_rtos.h"   /* 测试台：显式拨动仿真时钟以验证 update_tick */

void TestSuite_MotorManager(void)
{
    MotorData snap;
    MotorData copy;

    /* ------------------------------------------------------------------ */
    TC_BEGIN("MotorManager_Init：状态为 MOTOR_INIT，全部数据清零（ICD §22）");
    {
        MotorManager_Init();
        TC_CHECK(MotorManager_GetState() == MOTOR_INIT);
        TC_NEAR(MotorManager_GetTargetSpeed(), 0.0f, 1e-9);
        TC_NEAR(MotorManager_GetActualSpeed(), 0.0f, 1e-9);
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-9);
        TC_NEAR(MotorManager_GetPosition(), 0.0f, 1e-9);
        snap = MotorManager_GetSnapshot();
        TC_CHECK(snap.update_tick == 0U);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("SetTargetSpeed 参数校验（ICD §21 普通参数错误 -> RC_INVALID_PARAM）");
    {
        MotorManager_Init();
        TC_CHECK(MotorManager_SetTargetSpeed(1000.0f) == RC_OK);
        TC_NEAR(MotorManager_GetTargetSpeed(), 1000.0f, 1e-6);

        TC_CHECK(MotorManager_SetTargetSpeed(RC_MAX_TARGET_SPEED + 1.0f) == RC_INVALID_PARAM);
        TC_CHECK(MotorManager_SetTargetSpeed(-RC_MAX_TARGET_SPEED - 1.0f) == RC_INVALID_PARAM);
        TC_NEAR(MotorManager_GetTargetSpeed(), 1000.0f, 1e-6);   /* 非法值不得污染原值 */

        /* 边界值应当被接受 */
        TC_CHECK(MotorManager_SetTargetSpeed(RC_MAX_TARGET_SPEED) == RC_OK);
        TC_CHECK(MotorManager_SetTargetSpeed(-RC_MAX_TARGET_SPEED) == RC_OK);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("GetSnapshot 返回值拷贝，不是可写全局指针（ICD §9）");
    {
        MotorManager_Init();
        (void)MotorManager_SetTargetSpeed(500.0f);
        snap = MotorManager_GetSnapshot();
        copy = snap;
        copy.target_speed = 9999.0f;          /* 改副本 */
        copy.state        = MOTOR_RUNNING;
        TC_NEAR(MotorManager_GetTargetSpeed(), 500.0f, 1e-6);   /* 管理器不受影响 */
        TC_CHECK(MotorManager_GetState() == MOTOR_INIT);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("ICD §10 合法转换全部被接受");
    {
        MotorManager_Init();
        TC_CHECK(MotorManager_SetState(MOTOR_READY)   == RC_OK);   /* INIT -> READY */
        TC_CHECK(MotorManager_SetState(MOTOR_RUNNING) == RC_OK);   /* READY -> RUNNING */
        TC_CHECK(MotorManager_SetState(MOTOR_STOPPED) == RC_OK);   /* RUNNING -> STOPPED */
        TC_CHECK(MotorManager_SetState(MOTOR_RUNNING) == RC_OK);   /* STOPPED -> RUNNING */
        TC_CHECK(MotorManager_SetState(MOTOR_STOPPED) == RC_OK);
        TC_CHECK(MotorManager_SetState(MOTOR_READY)   == RC_OK);   /* STOPPED -> READY */
        TC_CHECK(MotorManager_SetState(MOTOR_ERROR)   == RC_OK);   /* READY -> ERROR */
        TC_CHECK(MotorManager_SetState(MOTOR_INIT)    == RC_OK);   /* ERROR -> INIT */
        TC_CHECK(MotorManager_SetState(MOTOR_READY)   == RC_OK);   /* INIT -> READY */
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("ICD §10 禁止转换必须被拒绝（RUNNING->INIT / ERROR->RUNNING / INIT->RUNNING）");
    {
        MotorManager_Init();
        TC_CHECK(MotorManager_SetState(MOTOR_RUNNING) == RC_INVALID_PARAM);  /* INIT -> RUNNING */

        TC_CHECK(MotorManager_SetState(MOTOR_READY)   == RC_OK);
        TC_CHECK(MotorManager_SetState(MOTOR_RUNNING) == RC_OK);
        TC_CHECK(MotorManager_SetState(MOTOR_INIT)    == RC_INVALID_PARAM);  /* RUNNING -> INIT */

        TC_CHECK(MotorManager_SetState(MOTOR_ERROR)   == RC_OK);
        TC_CHECK(MotorManager_SetState(MOTOR_RUNNING) == RC_INVALID_PARAM);  /* ERROR -> RUNNING */

        /* ERROR -> READY 属于「仅 RESET 后」，SetState 不得放行 */
        TC_CHECK(MotorManager_SetState(MOTOR_READY)   == RC_INVALID_PARAM);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("ERROR -> READY 只能经 RequestReset 完成（ICD §10「仅 RESET 后」）");
    {
        MotorManager_Init();
        TC_CHECK(MotorManager_SetState(MOTOR_READY) == RC_OK);
        TC_CHECK(MotorManager_SetState(MOTOR_ERROR) == RC_OK);
        TC_CHECK(MotorManager_RequestReset()        == RC_OK);
        TC_CHECK(MotorManager_GetState() == MOTOR_READY);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("RequestReset 从任意状态都能归位到 READY，且不违反 §10");
    {
        const MotorState starts[5] = { MOTOR_INIT, MOTOR_READY, MOTOR_RUNNING,
                                       MOTOR_STOPPED, MOTOR_ERROR };
        for (int i = 0; i < 5; ++i) {
            MotorManager_Init();
            /* Init 后固定处于 INIT，先走到目标起始状态 */
            if (starts[i] != MOTOR_INIT) {
                TC_CHECK(MotorManager_SetState(MOTOR_READY) == RC_OK);
            }
            if (starts[i] == MOTOR_RUNNING) {
                TC_CHECK(MotorManager_SetState(MOTOR_RUNNING) == RC_OK);
            } else if (starts[i] == MOTOR_STOPPED) {
                TC_CHECK(MotorManager_SetState(MOTOR_STOPPED) == RC_OK);
            } else if (starts[i] == MOTOR_ERROR) {
                TC_CHECK(MotorManager_SetState(MOTOR_ERROR) == RC_OK);
            }
            TC_CHECK(MotorManager_RequestReset() == RC_OK);
            TC_CHECK(MotorManager_GetState() == MOTOR_READY);
            TC_CHECK(MotorManager_TakeResetRequest() == true);
            TC_CHECK(MotorManager_TakeResetRequest() == false);   /* 读后清零 */
        }
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("安全不变式：非 RUNNING 状态下不接受非零输出");
    {
        MotorManager_Init();
        TC_CHECK(MotorManager_SetState(MOTOR_READY) == RC_OK);
        TC_CHECK(MotorManager_SetControlOutput(77.0f) == RC_OK);
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-9);   /* INIT/READY 下被强制 0 */

        TC_CHECK(MotorManager_SetState(MOTOR_RUNNING) == RC_OK);
        TC_CHECK(MotorManager_SetControlOutput(77.0f) == RC_OK);
        TC_NEAR(MotorManager_GetControlOutput(), 77.0f, 1e-6);  /* RUNNING 下正常写入 */

        TC_CHECK(MotorManager_SetState(MOTOR_STOPPED) == RC_OK);
        (void)MotorManager_SetControlOutput(77.0f);
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-9);   /* §27：STOP 后 output = 0 */
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("SetControlOutput 的 NaN/Inf 防御与硬限幅（ICD §16 / §21）");
    {
        float inf = 1.0f;
        float nan;
        MotorManager_Init();
        (void)MotorManager_SetState(MOTOR_READY);
        (void)MotorManager_SetState(MOTOR_RUNNING);

        /* 构造 Inf 与 NaN：避免依赖 math.h 的宏在嵌入式上的可用性 */
        inf = inf / 0.0f;
        nan = inf - inf;

        TC_CHECK(MotorManager_SetControlOutput(nan) == RC_INVALID_PARAM);
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-9);   /* fail-safe */

        TC_CHECK(MotorManager_SetControlOutput(inf) == RC_INVALID_PARAM);
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-9);

        TC_CHECK(MotorManager_SetControlOutput(1e6f) == RC_OK);
        TC_NEAR(MotorManager_GetControlOutput(), RC_DEFAULT_OUTPUT_LIMIT, 1e-6);   /* 硬限幅 */
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("StopOutput 立即清零（ICD §9 / §16）");
    {
        MotorManager_Init();
        (void)MotorManager_SetState(MOTOR_READY);
        (void)MotorManager_SetState(MOTOR_RUNNING);
        (void)MotorManager_SetControlOutput(50.0f);
        MotorManager_StopOutput();
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-9);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("进入 ERROR 时输出被强制清零（ICD §16 故障保护）");
    {
        MotorManager_Init();
        (void)MotorManager_SetState(MOTOR_READY);
        (void)MotorManager_SetState(MOTOR_RUNNING);
        (void)MotorManager_SetControlOutput(60.0f);
        TC_NEAR(MotorManager_GetControlOutput(), 60.0f, 1e-6);
        (void)MotorManager_SetState(MOTOR_ERROR);
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-9);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("UpdateFeedback 打时间戳（ICD §4 update_tick）");
    {
        sim_rtos_reset();
        MotorManager_Init();
        MotorManager_UpdateFeedback(123.0f, 45.0f);
        snap = MotorManager_GetSnapshot();
        TC_NEAR(snap.actual_speed, 123.0f, 1e-6);
        TC_NEAR(snap.position, 45.0f, 1e-6);
        TC_CHECK(snap.update_tick == RC_Port_GetTick());

        sim_rtos_advance_tick(37U);
        MotorManager_UpdateFeedback(200.0f, 90.0f);
        snap = MotorManager_GetSnapshot();
        TC_CHECK(snap.update_tick == 37U);
    }

    TC_SUITE_REPORT("MotorManager");
}
