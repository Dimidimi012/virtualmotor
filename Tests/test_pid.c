/**
 * @file    test_pid.c
 * @brief   Control/PID 单元测试 —— ICD §28 阶段 A
 *
 * 覆盖点与 ICD 条文的对应关系写在每个用例的注释里。
 */

#include "test_framework.h"
#include "pid.h"
#include "config.h"

#define TOL   (1e-5)

void TestSuite_PID(void)
{
    PIDController pid;

    /* ------------------------------------------------------------------ */
    TC_BEGIN("PID_Init 清零运行态（ICD §6：参数只在初始化阶段设定）");
    {
        PID_Init(&pid, 1.0f, 2.0f, 3.0f, 100.0f, 50.0f);
        TC_NEAR(pid.kp, 1.0f, TOL);
        TC_NEAR(pid.ki, 2.0f, TOL);
        TC_NEAR(pid.kd, 3.0f, TOL);
        TC_NEAR(pid.output_limit, 100.0f, TOL);
        TC_NEAR(pid.integral_limit, 50.0f, TOL);
        TC_NEAR(pid.integral, 0.0f, TOL);
        TC_NEAR(pid.last_error, 0.0f, TOL);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("纯比例：ki=kd=0 时 output == kp*error（ICD §7 第 ④ 行）");
    {
        PID_Init(&pid, 0.5f, 0.0f, 0.0f, 1000.0f, 1000.0f);
        TC_NEAR(PID_Update(&pid, 10.0f, 4.0f, 0.01f), 3.0f, TOL);
        TC_NEAR(PID_Update(&pid, 10.0f, 4.0f, 0.01f), 3.0f, TOL);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("积分累加：integral == sum(error*dt)（ICD §7 第 ② 行）");
    {
        PID_Init(&pid, 0.0f, 1.0f, 0.0f, 1e6f, 1e6f);
        /* 10 拍、每拍误差 2.0、dt=0.01 -> integral = 2.0*0.01*10 = 0.2 */
        for (int i = 0; i < 10; ++i) {
            PID_Update(&pid, 2.0f, 0.0f, 0.01f);
        }
        TC_NEAR(pid.integral, 0.2f, TOL);

        /* 每拍的输出应当等于当时累计的积分（ki=1）：
         * 第 1 拍 0.02、第 2 拍 0.04 …… 第 11 拍 0.22 */
        TC_NEAR(PID_Update(&pid, 2.0f, 0.0f, 0.01f), 0.22f, TOL);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("微分项：首拍 derivative = (error - 0)/dt（ICD §7 第 ③ 行）");
    {
        PID_Init(&pid, 0.0f, 0.0f, 1.0f, 1e6f, 1e6f);
        /* error=5, last_error=0, dt=0.01 -> derivative=500 -> output=500 */
        TC_NEAR(PID_Update(&pid, 5.0f, 0.0f, 0.01f), 500.0f, 1e-2);
        /* 误差不变时 derivative 归零 */
        TC_NEAR(PID_Update(&pid, 5.0f, 0.0f, 0.01f), 0.0f, TOL);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("输出限幅 clamp(output, +-output_limit)（ICD §7 第 ⑥ 行）");
    {
        PID_Init(&pid, 10.0f, 0.0f, 0.0f, 100.0f, 1000.0f);
        TC_NEAR(PID_Update(&pid, 1000.0f, 0.0f, 0.01f), 100.0f, TOL);
        TC_NEAR(PID_Update(&pid, -1000.0f, 0.0f, 0.01f), -100.0f, TOL);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("积分限幅 clamp(integral, +-integral_limit)（ICD §7 第 ⑤ 行）");
    {
        PID_Init(&pid, 1.0f, 1.0f, 0.0f, 1e6f, 5.0f);
        for (int i = 0; i < 1000; ++i) {
            PID_Update(&pid, 100.0f, 0.0f, 0.01f);
        }
        TC_NEAR(pid.integral, 5.0f, TOL);     /* 被钳在正限幅 */
        for (int i = 0; i < 2000; ++i) {
            PID_Update(&pid, -100.0f, 0.0f, 0.01f);
        }
        TC_NEAR(pid.integral, -5.0f, TOL);    /* 被钳在负限幅 */
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("dt <= 0 安全退出且不修改状态（ICD §7 / §21）");
    {
        PID_Init(&pid, 1.0f, 1.0f, 1.0f, 100.0f, 50.0f);
        (void)PID_Update(&pid, 10.0f, 0.0f, 0.01f);   /* 先制造非零状态 */

        {
            const float integral_before   = pid.integral;
            const float last_error_before = pid.last_error;

            TC_NEAR(PID_Update(&pid, 10.0f, 0.0f, 0.0f),  0.0f, TOL);
            TC_NEAR(PID_Update(&pid, 10.0f, 0.0f, -0.01f), 0.0f, TOL);

            TC_NEAR(pid.integral,   integral_before,   TOL);
            TC_NEAR(pid.last_error, last_error_before, TOL);
        }
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("PID_Reset 只清运行态，不动增益与限幅（ICD §27 RESET 语义）");
    {
        PID_Init(&pid, 0.7f, 0.3f, 0.1f, 100.0f, 50.0f);
        (void)PID_Update(&pid, 30.0f, 0.0f, 0.01f);
        PID_Reset(&pid);
        TC_NEAR(pid.integral, 0.0f, TOL);
        TC_NEAR(pid.last_error, 0.0f, TOL);
        TC_NEAR(pid.kp, 0.7f, TOL);
        TC_NEAR(pid.ki, 0.3f, TOL);
        TC_NEAR(pid.kd, 0.1f, TOL);
        TC_NEAR(pid.output_limit, 100.0f, TOL);
        TC_NEAR(pid.integral_limit, 50.0f, TOL);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("空指针防御：PID_Update(NULL,...) 与 PID_Reset(NULL) 不得崩溃");
    {
        TC_NEAR(PID_Update(0, 1.0f, 0.0f, 0.01f), 0.0f, TOL);
        PID_Reset(0);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("已知缺陷留证：无抗饱和时积分项在饱和期间继续累积（CR-001）");
    {
        /* 这不是"期望正确"，而是把 ICD §7 冻结算法的已知行为固化成可回归的证据。
         * 现象：输出早已被限幅，积分器仍按 error*dt 继续长；
         *       目标反向时，积分需要额外时间"爬回来"，表现为超调。 */
        PID_Init(&pid, 0.18f, 1.0f, 0.0f, 100.0f, 100.0f);
        for (int i = 0; i < 100; ++i) {
            (void)PID_Update(&pid, 1000.0f, 0.0f, 0.01f);   /* 1 秒持续大误差 */
        }
        TC_NEAR(pid.integral, 100.0f, 1e-3);   /* 已经顶到积分限幅 */

        /* 证据一：误差已经归零，控制器却仍然输出满幅 100。
         * 这就是「积分饱和」在数值上的定义 —— 输出不再由当前误差决定。 */
        TC_NEAR(PID_Update(&pid, 0.0f, 0.0f, 0.01f), 100.0f, 1e-3);
        TC_NEAR(pid.last_error, 0.0f, 1e-6);   /* 误差确实是 0 */

        /* 证据二：反向纠正极为迟缓。误差 -1 持续 1 秒（100 拍、dt=0.01）
         * 只能把积分从 100 拉回 1.0，即还需要 100 秒才能清空。
         * 现场表现就是「超调后迟迟不回来」——这正是 CR-001 要解决的问题。 */
        for (int i = 0; i < 100; ++i) {
            (void)PID_Update(&pid, -1.0f, 0.0f, 0.01f);
        }
        /* 100 拍 * 1.0 * 0.01 = 1.0，故积分应为 99.0。
         * 用容差比较而不是 > 99.0f：累加 100 次浮点后恰好等于 99.0f 不可依赖。 */
        TC_NEAR(pid.integral, 99.0f, 0.05f);
    }

    TC_SUITE_REPORT("PID");
}
