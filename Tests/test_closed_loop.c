/**
 * @file    test_closed_loop.c
 * @brief   端到端集成测试 —— 对应 ICD V1.0 §27「第一版最小可运行验证」
 *
 * 与前几个套件的本质区别：
 *   这里跑的是**真实的调度器 + 真实的 5 个任务 + 真实的消息队列**，
 *   不是把模块函数按顺序调一遍。因此它能抓到只有并发/时序才会暴露的缺陷
 *   （例如任务没被创建、优先级写反、心跳没上报、队列没人消费）。
 *
 * 激励方式：主机仿真串口按 tick 注入文本命令，等价于有人在终端上敲命令。
 */

#include "test_framework.h"

#include "app.h"
#include "motor_manager.h"
#include "monitor_service.h"
#include "command_service.h"
#include "config.h"
#include "rc_port.h"
#include "sim_rtos.h"
#include "bsp_uart.h"
#include "bsp_uart_sim.h"

#include "task.h"
#include "queue.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * 场景任务：在指定 tick 挂起 / 恢复另一个任务。
 *
 * 这是「时序型故障注入」——比单纯让 MotorTask 一开始就不跑更接近现场：
 * 电机先正常工作，某一刻反馈线断了，Monitor 必须在有限时间内发现。
 * 它属于测试台，不属于产品代码，因此只出现在这里。
 * ---------------------------------------------------------------------- */
#define TC_SCENARIO_MAX  (4U)

typedef struct {
    TaskHandle_t target;
    TickType_t   tick;
    bool         do_suspend;
} TcScenario;

static TcScenario s_scenarios[TC_SCENARIO_MAX];
static uint32_t   s_scenario_count;

static void tc_scenario_task(void *arg)
{
    TcScenario *s = (TcScenario *)arg;
    TickType_t  last = xTaskGetTickCount();

    vTaskDelayUntil(&last, s->tick);

    if (s->do_suspend) {
        vTaskSuspend(s->target);
    } else {
        vTaskResume(s->target);
    }

    for (;;) {
        vTaskDelay((TickType_t)1000);
    }
}

static void tc_add_scenario(TaskHandle_t target, TickType_t tick, bool suspend)
{
    if (s_scenario_count >= TC_SCENARIO_MAX) {
        return;
    }

    s_scenarios[s_scenario_count].target     = target;
    s_scenarios[s_scenario_count].tick       = tick;
    s_scenarios[s_scenario_count].do_suspend = suspend;

    (void)xTaskCreate(tc_scenario_task, "scenario", 256,
                      &s_scenarios[s_scenario_count],
                      TASK_PRIO_DEBUG, 0);

    ++s_scenario_count;
}

/* -------------------------------------------------------------------------
 * 测试台辅助
 * ---------------------------------------------------------------------- */
static void tc_reset(void)
{
    sim_rtos_reset();
    BSP_UART_Sim_Reset();
    BSP_UART_Sim_SetEcho(false);   /* 测试时不要污染控制台 */
    s_scenario_count = 0U;
}

static RC_Result tc_boot(void)
{
    const RC_Result rc = App_Init();
    if (rc != RC_OK) {
        return rc;
    }
    return App_CreateTasks();
}

static void tc_run(const BSP_UART_SimEntry *script, uint32_t count, uint32_t ticks)
{
    BSP_UART_Sim_LoadScript(script, count);
    sim_rtos_set_tick_limit((TickType_t)ticks);
    vTaskStartScheduler();
}

/** @brief 统计捕获输出中某个子串出现的次数 */
static uint32_t tc_count(const char *needle)
{
    const char *hay = BSP_UART_Sim_GetOutput();
    uint32_t    n = 0U;
    const size_t nlen = strlen(needle);

    if ((hay == 0) || (nlen == 0U)) {
        return 0U;
    }
    while ((hay = strstr(hay, needle)) != 0) {
        ++n;
        hay += nlen;
    }
    return n;
}

/* =========================================================================
 * TestSuite_ClosedLoop
 * ====================================================================== */
void TestSuite_ClosedLoop(void)
{
    /* ------------------------------------------------------------------ */
    TC_BEGIN("ICD §27-1：启动系统后 state = READY（冻结启动顺序 §22）");
    {
        tc_reset();
        TC_CHECK(tc_boot() == RC_OK);
        TC_CHECK(MotorManager_GetState() == MOTOR_READY);
        TC_NEAR(MotorManager_GetTargetSpeed(), 0.0f, 1e-9);
        TC_NEAR(MotorManager_GetActualSpeed(), 0.0f, 1e-9);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("ICD §27-2/3/4/5/6：START -> RUNNING，SET_SPEED 1000 后闭环收敛");
    {
        static const BSP_UART_SimEntry script[] = {
            { 200U, "START"          },
            { 400U, "SET_SPEED 1000" },
        };

        tc_reset();
        TC_CHECK(tc_boot() == RC_OK);

        /* 跑 8 秒：足够让 10 ms 闭环从 0 收敛到稳态 */
        tc_run(script, 2U, 8000U);

        TC_CHECK(MotorManager_GetState() == MOTOR_RUNNING);
        TC_NEAR(MotorManager_GetTargetSpeed(), 1000.0f, 0.01f);

        printf("    [info] 闭环终值 actual=%.4f output=%.4f pos=%.4f\n",
               (double)MotorManager_GetActualSpeed(),
               (double)MotorManager_GetControlOutput(),
               (double)MotorManager_GetPosition());

        /* 稳态误差应在 1% 以内 —— 有积分项时理论上应接近 0 */
        TC_NEAR(MotorManager_GetActualSpeed(), 1000.0f, 10.0f);

        /* 控制输出必须非零，否则说明 PID 根本没在驱动 */
        TC_CHECK(MotorManager_GetControlOutput() > 1.0f);

        /* 位置应当随速度累积（VirtualMotor 的 position 是 speed 的积分） */
        TC_CHECK(MotorManager_GetPosition() > 0.0f);

        /* 整个健康过程中不得出现任何故障 */
        TC_CHECK(MonitorService_GetFaults() == RC_FAULT_NONE);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("ICD §27-7：DebugTask 每 500 ms 输出一行（ICD §17 格式）");
    {
        static const BSP_UART_SimEntry script[] = {
            { 200U, "START"          },
            { 400U, "SET_SPEED 1000" },
        };
        uint32_t lines;

        tc_reset();
        TC_CHECK(tc_boot() == RC_OK);
        tc_run(script, 2U, 6000U);

        lines = tc_count("TGT=");
        printf("    [info] Debug 输出行数 = %u（6 秒 / 500 ms 期望约 12 行）\n", lines);
        TC_CHECK(lines >= 10U);            /* 允许启动阶段的相位损失 */
        TC_CHECK(lines <= 14U);            /* 也不允许刷屏：周期必须是 500 ms */
        TC_CHECK(tc_count("STATE=RUNNING") > 0U);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("ICD §27-8：STOP -> output = 0，state = STOPPED");
    {
        static const BSP_UART_SimEntry script[] = {
            { 200U, "START"          },
            { 400U, "SET_SPEED 1000" },
            { 3000U, "STOP"          },
        };

        tc_reset();
        TC_CHECK(tc_boot() == RC_OK);
        tc_run(script, 3U, 4000U);

        TC_CHECK(MotorManager_GetState() == MOTOR_STOPPED);
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-6);
        /* 目标速度保留：STOP 只停输出，不清设定值（ICD §27 未要求清零） */
        TC_NEAR(MotorManager_GetTargetSpeed(), 1000.0f, 0.01f);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("ICD §27-9：RESET -> PID_Reset + VirtualMotor 重置 + READY");
    {
        static const BSP_UART_SimEntry script[] = {
            { 200U,  "START"          },
            { 400U,  "SET_SPEED 1000" },
            { 3000U, "RESET"          },
        };

        tc_reset();
        TC_CHECK(tc_boot() == RC_OK);
        tc_run(script, 3U, 4500U);

        TC_CHECK(MotorManager_GetState() == MOTOR_READY);
        TC_NEAR(MotorManager_GetTargetSpeed(), 0.0f, 1e-6);
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-6);
        /* VirtualMotor 必须真的被复位：速度与位置都回到 0 */
        TC_NEAR(MotorManager_GetActualSpeed(), 0.0f, 1e-3);
        TC_NEAR(MotorManager_GetPosition(), 0.0f, 1e-3);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("ICD §27-10：故障注入（停止 Motor 更新）-> Monitor 最终 ERROR");
    {
        static const BSP_UART_SimEntry script[] = {
            { 200U, "START"          },
            { 400U, "SET_SPEED 1000" },
        };

        tc_reset();
        TC_CHECK(tc_boot() == RC_OK);

        /* 故障注入：直接把 MotorTask 挂起，等价于"电机反馈线断了"。
         * ControlTask 会继续用过期反馈跑 PID —— 这正是 Monitor 存在的意义。 */
        vTaskSuspend(App_GetTaskHandle(APP_TASK_MOTOR));

        tc_run(script, 2U, 3000U);

        TC_CHECK(MotorManager_GetState() == MOTOR_ERROR);
        TC_CHECK((MonitorService_GetFaults() & RC_FAULT_MOTOR_FB) != 0U);
        /* 保护动作必须真正切断执行器（ICD §16） */
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-6);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("命令层面的故障（解析失败）只记录、不急停（config.h B8b）");
    {
        static const BSP_UART_SimEntry script[] = {
            { 200U, "START"          },
            { 400U, "SET_SPEED 1000" },
            { 800U, "THIS_IS_NOT_A_COMMAND" },
            { 1000U, "SET_SPEED abc" },
        };

        tc_reset();
        TC_CHECK(tc_boot() == RC_OK);
        tc_run(script, 4U, 4000U);

        /* 故障被记录下来了 */
        TC_CHECK(CommandService_GetParseRejectCount() >= 2U);
        TC_CHECK((MonitorService_GetFaults() & RC_FAULT_CMD_PARSE) != 0U);

        /* 但系统必须继续正常运行 —— 这正是"可用性不能被升级成安全问题" */
        TC_CHECK(MotorManager_GetState() == MOTOR_RUNNING);
        TC_NEAR(MotorManager_GetActualSpeed(), 1000.0f, 10.0f);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("非法状态转换：ERROR 状态下不收 START（ICD §10 禁止 ERROR -> RUNNING）");
    {
        static const BSP_UART_SimEntry script[] = {
            { 200U, "START"          },
            { 400U, "SET_SPEED 1000" },
            { 900U, "START"          },   /* 已经在 ERROR，应被拒绝 */
        };

        tc_reset();
        TC_CHECK(tc_boot() == RC_OK);

        /* 注入故障让系统进入 ERROR */
        vTaskSuspend(App_GetTaskHandle(APP_TASK_MOTOR));
        tc_run(script, 3U, 3000U);

        TC_CHECK(MotorManager_GetState() == MOTOR_ERROR);
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-6);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("RESET 不能掩盖仍然存在的故障：故障源未消失时必须重新 ERROR");
    {
        static const BSP_UART_SimEntry script[] = {
            { 200U,  "START"          },
            { 400U,  "SET_SPEED 1000" },
            { 2000U, "RESET"          },
        };

        tc_reset();
        TC_CHECK(tc_boot() == RC_OK);

        /* MotorTask 始终不跑 = 反馈永远不会更新，故障源一直在 */
        vTaskSuspend(App_GetTaskHandle(APP_TASK_MOTOR));
        tc_run(script, 3U, 4000U);

        /* 这是刻意的设计，不是缺陷：
         * 如果一次 RESET 就能让"没有反馈"的系统停在 READY，
         * 现场会立刻养成"按一下复位接着跑"的危险习惯。
         * 保护的语义是"故障源消失才允许恢复"，而不是"清一下标志位"。 */
        TC_CHECK(MotorManager_GetState() == MOTOR_ERROR);
        TC_CHECK((MonitorService_GetFaults() & RC_FAULT_MOTOR_FB) != 0U);
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-6);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("故障源恢复后 RESET -> 回到 READY 且无残留故障（ICD §10「仅 RESET 后」）");
    {
        static const BSP_UART_SimEntry script[] = {
            { 200U,  "START"          },
            { 400U,  "SET_SPEED 1000" },
            { 3000U, "RESET"          },
        };

        tc_reset();
        TC_CHECK(tc_boot() == RC_OK);

        /* 时序型故障注入：0.5 s 断反馈 -> 2.5 s 恢复 -> 3.0 s RESET */
        tc_add_scenario(App_GetTaskHandle(APP_TASK_MOTOR), 500U,  true);
        tc_add_scenario(App_GetTaskHandle(APP_TASK_MOTOR), 2500U, false);

        tc_run(script, 3U, 6000U);

        TC_CHECK(MotorManager_GetState() == MOTOR_READY);
        TC_CHECK(MonitorService_GetFaults() == RC_FAULT_NONE);
        TC_NEAR(MotorManager_GetTargetSpeed(), 0.0f, 1e-6);
        TC_NEAR(MotorManager_GetControlOutput(), 0.0f, 1e-6);
        /* VirtualMotor 已被复位，恢复反馈后速度应从 0 重新开始 */
        TC_NEAR(MotorManager_GetActualSpeed(), 0.0f, 1e-3);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("正常 STOP -> START 往返不产生任何故障");
    {
        static const BSP_UART_SimEntry script[] = {
            { 200U,  "START"          },
            { 400U,  "SET_SPEED 800"  },
            { 1500U, "STOP"           },
            { 2000U, "START"          },
            { 2200U, "SET_SPEED 800"  },
        };

        tc_reset();
        TC_CHECK(tc_boot() == RC_OK);
        tc_run(script, 5U, 5000U);

        TC_CHECK(MotorManager_GetState() == MOTOR_RUNNING);
        TC_CHECK(MonitorService_GetFaults() == RC_FAULT_NONE);
        TC_NEAR(MotorManager_GetActualSpeed(), 800.0f, 10.0f);
    }

    TC_SUITE_REPORT("ClosedLoop");
}
