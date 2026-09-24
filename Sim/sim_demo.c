/**
 * @file    sim_demo.c
 * @brief   主机仿真演示程序 —— 对应 ICD V1.0 §27「第一版最小可运行验证」与 §28 阶段 E
 *
 * 它和 Core/main.c 的关系：
 *   产品入口是 Core/main.c（只做 App_Init / App_CreateTasks / vTaskStartScheduler 三步）。
 *   本文件是「演示与数据采集台」，调用完全相同的三步，并额外挂一个采样任务，
 *   用来把闭环过程导出成 CSV，供画曲线与答辩展示。
 *   两者共用同一套任务代码、同一套服务模块，没有任何"演示专用分支"。
 *
 * 用法：
 *   sim_demo.exe [normal|fault] [csv_path]
 *     normal —— 完整功能演示：STATUS/START/SET_SPEED 阶跃/反向/STOP/RESET/非法命令
 *     fault  —— 故障注入演示：运行中切断电机反馈 -> Monitor 保护 -> ERROR
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "config.h"
#include "types.h"
#include "motor_manager.h"
#include "monitor_service.h"
#include "command_service.h"

#include "bsp_uart.h"
#include "bsp_uart_sim.h"
#include "sim_rtos.h"

#include "task.h"
#include "queue.h"

/* =========================================================================
 * 演示场景定义
 * ====================================================================== */
typedef enum {
    DEMO_MODE_NORMAL = 0,
    DEMO_MODE_FAULT
} DemoMode;

/* --- 正常场景：把 ICD §27 的验收流程按顺序走一遍 --- */
static const BSP_UART_SimEntry s_script_normal[] = {
    {  500U, "STATUS"           },
    { 1000U, "START"            },
    { 1500U, "SET_SPEED 1000"   },
    { 5000U, "SET_SPEED 1500"   },
    { 8000U, "SET_SPEED -800"   },
    {11000U, "STOP"             },
    {11600U, "RESET"            },
    {12000U, "STATUS"           },
    {12500U, "THIS IS NOT A COMMAND" },
    {12800U, "SET_SPEED 999999" },   /* 越界：应被拒绝且不污染原目标 */
    {13200U, "START"            },
    {13600U, "SET_SPEED 600"    },
};

/* --- 故障注入场景：4 秒时切断电机反馈 --- */
static const BSP_UART_SimEntry s_script_fault[] = {
    {  500U, "START"          },
    { 1000U, "SET_SPEED 1000" },
};

#define DEMO_SAMPLE_PERIOD_MS   (10U)
#define DEMO_NORMAL_DURATION_MS (15000U)
#define DEMO_FAULT_DURATION_MS  (8000U)
#define DEMO_FAULT_INJECT_MS    (4000U)

static FILE     *s_csv;
static DemoMode  s_mode;
static int       s_fault_injected;

/* =========================================================================
 * 采样任务（演示台专用，不属于产品代码）
 *
 * 只读 MotorManager API，不写任何共享状态，因此不会干扰控制闭环 ——
 * 这一点很重要：观测动作本身不能改变被观测对象。
 * ====================================================================== */
static void demo_sampler_task(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)argument;

    for (;;) {
        MotorData snap;

        vTaskDelayUntil(&last_wake, (TickType_t)pdMS_TO_TICKS(DEMO_SAMPLE_PERIOD_MS));

        snap = MotorManager_GetSnapshot();

        if (s_csv != 0) {
            (void)fprintf(s_csv,
                          "%lu,%.6f,%.6f,%.6f,%.6f,%s,0x%08lX\n",
                          (unsigned long)xTaskGetTickCount(),
                          (double)snap.target_speed,
                          (double)snap.actual_speed,
                          (double)snap.control_output,
                          (double)snap.position,
                          MotorState_Name(snap.state),
                          (unsigned long)MonitorService_GetFaults());
        }

        /* 故障注入：在指定时刻切断电机反馈（等价于反馈线脱落） */
        if ((s_mode == DEMO_MODE_FAULT) && (s_fault_injected == 0) &&
            (xTaskGetTickCount() >= (TickType_t)DEMO_FAULT_INJECT_MS)) {
            s_fault_injected = 1;
            vTaskSuspend(App_GetTaskHandle(APP_TASK_MOTOR));
            BSP_UART_WriteLine("[scenario] fault injected: motor feedback line cut");
        }
    }
}

/* =========================================================================
 * main
 * ====================================================================== */
int main(int argc, char **argv)
{
    const BSP_UART_SimEntry *script;
    uint32_t                 script_count;
    uint32_t                 duration_ms;
    const char              *mode_name;
    const char              *csv_path;
    RC_Result                rc;
    uint32_t                 i;

    if ((argc > 1) && (strcmp(argv[1], "fault") == 0)) {
        s_mode     = DEMO_MODE_FAULT;
        mode_name  = "fault";
        script     = s_script_fault;
        script_count = (uint32_t)(sizeof(s_script_fault) / sizeof(s_script_fault[0]));
        duration_ms  = DEMO_FAULT_DURATION_MS;
    } else {
        s_mode     = DEMO_MODE_NORMAL;
        mode_name  = "normal";
        script     = s_script_normal;
        script_count = (uint32_t)(sizeof(s_script_normal) / sizeof(s_script_normal[0]));
        duration_ms  = DEMO_NORMAL_DURATION_MS;
    }

    csv_path = (argc > 2) ? argv[2] : "trace.csv";

    setvbuf(stdout, NULL, _IONBF, 0);

    BSP_UART_Sim_Reset();
    BSP_UART_Sim_SetEcho(true);
    BSP_UART_Sim_SetScriptEcho(true);

    printf("\n");
    printf("#############################################################\n");
    printf("#  " RC_VERSION_STRING "  |  host simulation demo\n");
    printf("#  mode = %s , duration = %lu ms\n", mode_name, (unsigned long)duration_ms);
    printf("#############################################################\n");
    printf("#  scenario:\n");
    for (i = 0U; i < script_count; ++i) {
        printf("#    t=%6lu ms  =>  %s\n",
               (unsigned long)script[i].tick, script[i].line);
    }
    printf("#############################################################\n\n");

    /* ---- ICD §22 冻结启动顺序 ------------------------------------------ */
    rc = App_Init();
    if (rc != RC_OK) {
        printf("BOOT FAILED (rc=%s)\n", RC_Result_Name(rc));
        return 1;
    }

    rc = App_CreateTasks();
    if (rc != RC_OK) {
        printf("TASK CREATE FAILED\n");
        return 1;
    }

    /* ---- 演示台：采样任务（低优先级，绝不抢占控制任务） ---------------- */
    if (xTaskCreate(demo_sampler_task, "sampler", 256, 0,
                    TASK_PRIO_DEBUG, 0) != pdPASS) {
        printf("SAMPLER TASK CREATE FAILED\n");
        return 1;
    }

    /* ---- 打开 CSV ------------------------------------------------------ */
    s_csv = fopen(csv_path, "w");
    if (s_csv != 0) {
        (void)fprintf(s_csv,
                      "tick_ms,target_speed,actual_speed,control_output,position,state,faults\n");
        printf("# telemetry -> %s\n\n", csv_path);
    } else {
        printf("# WARNING: cannot open %s, telemetry disabled\n", csv_path);
    }

    /* ---- 装载激励脚本并运行 -------------------------------------------- */
    BSP_UART_Sim_LoadScript(script, script_count);
    sim_rtos_set_tick_limit((TickType_t)duration_ms);
    vTaskStartScheduler();

    if (s_csv != 0) {
        fclose(s_csv);
        s_csv = 0;
    }

    /* ---- 总结 ---------------------------------------------------------- */
    {
        MotorData snap = MotorManager_GetSnapshot();

        printf("\n#############################################################\n");
        printf("#  simulation finished at t = %lu ms\n", (unsigned long)sim_rtos_get_tick());
        printf("#  final state   : %s\n", MotorState_Name(snap.state));
        printf("#  target / actual: %.2f / %.2f\n",
               (double)snap.target_speed, (double)snap.actual_speed);
        printf("#  control output : %.2f\n", (double)snap.control_output);
        printf("#  position       : %.2f\n", (double)snap.position);
        printf("#  faults         : 0x%08lX\n", (unsigned long)MonitorService_GetFaults());
        printf("#  cmd rejected   : queue_full=%lu parse_error=%lu\n",
               (unsigned long)CommandService_GetSendRejectCount(),
               (unsigned long)CommandService_GetParseRejectCount());
        printf("#############################################################\n");
    }

    return 0;
}
