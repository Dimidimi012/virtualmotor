/**
 * @file    test_config.c
 * @brief   集中配置的不变量测试
 *
 * 为什么单独一个套件：
 *   config.h 里的浮点不变量无法用 _Static_assert 表达（C11 要求整数常量表达式），
 *   但配置错误必须在每次构建时就被抓到，而不是等到现场调试。
 *   因此把这些不变量变成「每次构建都会跑的测试」。
 */

#include "test_framework.h"
#include "config.h"
#include "pid.h"
#include "virtual_motor.h"

void TestSuite_Config(void)
{
    /* ------------------------------------------------------------------ */
    TC_BEGIN("PID 增益与限幅必须为正且自洽");
    {
        TC_CHECK(RC_PID_OUTPUT_LIMIT > 0.0f);
        TC_CHECK(RC_PID_INTEGRAL_LIMIT > 0.0f);
        TC_CHECK(RC_PID_KI > 0.0f);
        TC_CHECK(RC_PID_KP >= 0.0f);
        TC_CHECK(RC_PID_KD >= 0.0f);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("积分限幅取值规则：ki * integral_limit == output_limit（CR-002）");
    {
        /* 该规则保证「积分项单独作用时最多达到满输出」，
         * 避免 ICD §20 默认值 1000 造成的积分独大。 */
        TC_NEAR(RC_PID_KI * RC_PID_INTEGRAL_LIMIT, RC_PID_OUTPUT_LIMIT, 1e-3);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("控制周期与 RC_DEFAULT_DT 必须一致（ICD §7 的隐含前提）");
    {
        TC_NEAR((float)RC_CONTROL_PERIOD_MS / 1000.0f, RC_DEFAULT_DT, 1e-9);
        TC_NEAR((float)RC_MOTOR_PERIOD_MS   / 1000.0f, RC_DEFAULT_DT, 1e-9);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("虚拟电机对象可达性：满输出稳态速度必须覆盖目标上限");
    {
        /* v_ss(max) = gain/damping * output_limit；
         * 若它小于 RC_MAX_TARGET_SPEED，任何接近上限的指令都会被输出限幅永久顶死，
         * 表现为"有静差且积分饱和"——这属于对象参数选错，不是 PID 参数问题。 */
        const float v_max = (RC_VMOTOR_GAIN / RC_VMOTOR_DAMPING) * RC_DEFAULT_OUTPUT_LIMIT;
        TC_CHECK(v_max >= RC_MAX_TARGET_SPEED);
        TC_CHECK(RC_VMOTOR_DAMPING > 0.0f);
        TC_CHECK(RC_VMOTOR_GAIN > 0.0f);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("任务优先级满足 ICD §12：Control/Motor=High > Command=Medium > Monitor/Debug=Low");
    {
        TC_CHECK(TASK_PRIO_CONTROL == TASK_PRIO_MOTOR);
        TC_CHECK(TASK_PRIO_CONTROL > TASK_PRIO_COMMAND);
        TC_CHECK(TASK_PRIO_COMMAND > TASK_PRIO_MONITOR);
        TC_CHECK(TASK_PRIO_MONITOR >= TASK_PRIO_DEBUG);
        TC_CHECK(TASK_PRIO_DEBUG >= 1U);
        TC_CHECK(TASK_PRIO_CONTROL < RC_CONFIG_MAX_PRIORITIES);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("任务栈深度为正（单位：StackType_t 字数）");
    {
        TC_CHECK(TASK_STACK_CONTROL > 0U);
        TC_CHECK(TASK_STACK_MOTOR   > 0U);
        TC_CHECK(TASK_STACK_COMMAND > 0U);
        TC_CHECK(TASK_STACK_MONITOR > 0U);
        TC_CHECK(TASK_STACK_DEBUG   > 0U);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Monitor 阈值必须比对应任务周期宽裕（否则会误报）");
    {
        TC_CHECK((uint32_t)RC_HB_TIMEOUT_CONTROL_MS > (uint32_t)RC_CONTROL_PERIOD_MS);
        TC_CHECK((uint32_t)RC_HB_TIMEOUT_MOTOR_MS   > (uint32_t)RC_MOTOR_PERIOD_MS);
        /* 心跳超时应当能容忍至少 3 拍丢失，否则一次调度抖动就报故障 */
        TC_CHECK((uint32_t)RC_HB_TIMEOUT_CONTROL_MS >= 3U * (uint32_t)RC_CONTROL_PERIOD_MS);
        TC_CHECK(RC_MONITOR_MAX_ABS_SPEED > RC_MAX_TARGET_SPEED);
        TC_CHECK(RC_MONITOR_MAX_ABS_OUTPUT > RC_DEFAULT_OUTPUT_LIMIT);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("故障分类：命令层面的故障不得触发保护动作（config.h B8b）");
    {
        /* 关键安全语义：串口打错字不应该让电机急停 */
        TC_CHECK((RC_FAULT_SEVERE_MASK & RC_FAULT_CMD_PARSE)  == 0U);
        TC_CHECK((RC_FAULT_SEVERE_MASK & RC_FAULT_QUEUE_FULL) == 0U);

        /* 闭环可信度相关的故障必须触发保护 */
        TC_CHECK((RC_FAULT_SEVERE_MASK & RC_FAULT_CONTROL_HB)  != 0U);
        TC_CHECK((RC_FAULT_SEVERE_MASK & RC_FAULT_MOTOR_FB)    != 0U);
        TC_CHECK((RC_FAULT_SEVERE_MASK & RC_FAULT_NO_RESPONSE) != 0U);
        TC_CHECK((RC_FAULT_SEVERE_MASK & RC_FAULT_NONFINITE)   != 0U);
        TC_CHECK((RC_FAULT_SEVERE_MASK & RC_FAULT_OUT_OF_RANGE)!= 0U);
        TC_CHECK(RC_FAULT_SEVERE_MASK != RC_FAULT_NONE);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("命令缓冲容量自洽");
    {
        TC_CHECK(RC_CMD_TOKEN_MAX >= 2U);
        TC_CHECK(RC_CMD_TOKEN_LEN  > 1U);
        TC_CHECK(RC_CMD_LINE_MAX   > RC_CMD_TOKEN_LEN);
    }

    TC_SUITE_REPORT("Config");
}
