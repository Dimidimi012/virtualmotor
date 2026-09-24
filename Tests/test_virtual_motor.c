/**
 * @file    test_virtual_motor.c
 * @brief   Simulation/VirtualMotor 单元测试 —— ICD §28 阶段 A
 */

#include "test_framework.h"
#include "virtual_motor.h"
#include "config.h"

void TestSuite_VirtualMotor(void)
{
    VirtualMotor m;

    /* ------------------------------------------------------------------ */
    TC_BEGIN("VirtualMotor_Init 载入集中配置并清零运行态（ICD §8 / §20）");
    {
        VirtualMotor_Init(&m);
        TC_NEAR(m.motor_gain, RC_VMOTOR_GAIN, 1e-6);
        TC_NEAR(m.damping,    RC_VMOTOR_DAMPING, 1e-6);
        TC_NEAR(m.load,       RC_VMOTOR_LOAD, 1e-6);
        TC_NEAR(m.speed, 0.0f, 1e-6);
        TC_NEAR(m.position, 0.0f, 1e-6);
        TC_NEAR(m.input, 0.0f, 1e-6);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("零输入零负载：速度与位置都不动");
    {
        VirtualMotor_Init(&m);
        for (int i = 0; i < 100; ++i) {
            VirtualMotor_Update(&m, 0.01f);
        }
        TC_NEAR(VirtualMotor_GetSpeed(&m), 0.0f, 1e-6);
        TC_NEAR(VirtualMotor_GetPosition(&m), 0.0f, 1e-6);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("单步精确性：严格实现 ICD §8 的加速度公式");
    {
        VirtualMotor_Init(&m);
        m.motor_gain = 100.0f;
        m.damping    = 2.0f;
        m.load       = 0.5f;
        VirtualMotor_SetInput(&m, 10.0f);

        /* a = 100*10 - 2*0 - 0.5 = 999.5 ; v = 0 + 999.5*0.01 = 9.995
         * p = 0 + 9.995*0.01 = 0.09995   （半隐式欧拉：用更新后的 v 积分 p） */
        VirtualMotor_Update(&m, 0.01f);
        TC_NEAR(VirtualMotor_GetSpeed(&m), 9.995f, 1e-4);
        TC_NEAR(VirtualMotor_GetPosition(&m), 0.09995f, 1e-6);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("半隐式欧拉验证：position 必须用更新后的 speed 积分");
    {
        VirtualMotor_Init(&m);
        m.motor_gain = 0.0f;      /* 去掉加速度项，让速度完全由初值决定 */
        m.damping    = 0.0f;
        m.load       = 0.0f;
        m.speed      = 3.0f;
        VirtualMotor_Update(&m, 0.1f);
        /* 若误用旧速度（0）积分，position 会是 0；正确实现是 3.0*0.1 = 0.3 */
        TC_NEAR(VirtualMotor_GetPosition(&m), 0.3f, 1e-6);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("稳态速度 = (gain*input - load)/damping（一阶模型解析解）");
    {
        VirtualMotor_Init(&m);
        m.motor_gain = 100.0f;
        m.damping    = 2.0f;
        m.load       = 0.0f;
        VirtualMotor_SetInput(&m, 20.0f);
        /* 解析稳态 v_ss = 100*20/2 = 1000 */
        for (int i = 0; i < 2000; ++i) {          /* 20 秒，tau=0.5s 的 40 倍 */
            VirtualMotor_Update(&m, 0.01f);
        }
        TC_NEAR(VirtualMotor_GetSpeed(&m), 1000.0f, 0.01);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("负载扰动降低稳态速度：v_ss = (gain*input - load)/damping");
    {
        VirtualMotor_Init(&m);
        m.motor_gain = 100.0f;
        m.damping    = 2.0f;
        VirtualMotor_SetInput(&m, 20.0f);
        VirtualMotor_SetLoad(&m, 200.0f);
        /* v_ss = (2000 - 200)/2 = 900 */
        for (int i = 0; i < 2000; ++i) {
            VirtualMotor_Update(&m, 0.01f);
        }
        TC_NEAR(VirtualMotor_GetSpeed(&m), 900.0f, 0.01);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("dt <= 0 不推进状态（防御非法步长）");
    {
        VirtualMotor_Init(&m);
        VirtualMotor_SetInput(&m, 10.0f);
        VirtualMotor_Update(&m, 0.01f);
        {
            const float v = m.speed;
            const float p = m.position;
            VirtualMotor_Update(&m, 0.0f);
            VirtualMotor_Update(&m, -0.01f);
            TC_NEAR(m.speed, v, 1e-9);
            TC_NEAR(m.position, p, 1e-9);
        }
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("VirtualMotor_Init 同时充当复位：RESET 后状态可复现（ICD §27）");
    {
        VirtualMotor_Init(&m);
        VirtualMotor_SetInput(&m, 50.0f);
        for (int i = 0; i < 50; ++i) { VirtualMotor_Update(&m, 0.01f); }
        TC_CHECK(m.speed != 0.0f);

        VirtualMotor_Init(&m);
        TC_NEAR(m.speed, 0.0f, 1e-9);
        TC_NEAR(m.position, 0.0f, 1e-9);
        TC_NEAR(m.input, 0.0f, 1e-9);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("空指针防御");
    {
        VirtualMotor_Init(0);
        VirtualMotor_Update(0, 0.01f);
        VirtualMotor_SetInput(0, 1.0f);
        VirtualMotor_SetLoad(0, 1.0f);
        TC_NEAR(VirtualMotor_GetSpeed(0), 0.0f, 1e-9);
        TC_NEAR(VirtualMotor_GetPosition(0), 0.0f, 1e-9);
    }

    TC_SUITE_REPORT("VirtualMotor");
}
