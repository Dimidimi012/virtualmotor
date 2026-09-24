/**
 * @file    app.c
 * @brief   应用层启动流程实现 —— 对应 ICD V1.0 第 22 节
 */

#include "app.h"

#include "types.h"
#include "config.h"
#include "rc_port.h"

#include "motor_manager.h"
#include "command_service.h"
#include "monitor_service.h"

#include "task_control.h"
#include "task_motor.h"
#include "task_command.h"
#include "task_monitor.h"
#include "task_debug.h"

#include "bsp_uart.h"

/* -------------------------------------------------------------------------
 * 任务句柄注册表（供仿真注入故障；产品逻辑不依赖它）
 * ---------------------------------------------------------------------- */
static TaskHandle_t s_handles[APP_TASK_COUNT];

/* -------------------------------------------------------------------------
 * 任务描述表：把 ICD §12 的任务表变成一张可核对的表，
 * 而不是散落在 5 个 xTaskCreate 调用里的参数。
 * ---------------------------------------------------------------------- */
typedef struct {
    TaskFunction_t fn;
    const char    *name;
    uint16_t       stack;
    UBaseType_t    prio;
} AppTaskDesc;

static const AppTaskDesc s_task_desc[APP_TASK_COUNT] = {
    /* ICD §12：ControlTask  High   10 ms  */
    { TaskControl_Run, "control", TASK_STACK_CONTROL, TASK_PRIO_CONTROL },
    /* ICD §12：MotorTask    High   10 ms  */
    { TaskMotor_Run,   "motor",   TASK_STACK_MOTOR,   TASK_PRIO_MOTOR   },
    /* ICD §12：CommandTask  Medium Event-driven */
    { TaskCommand_Run, "command", TASK_STACK_COMMAND, TASK_PRIO_COMMAND },
    /* ICD §12：MonitorTask  Low    100 ms */
    { TaskMonitor_Run, "monitor", TASK_STACK_MONITOR, TASK_PRIO_MONITOR },
    /* ICD §12：DebugTask    Low    500 ms */
    { TaskDebug_Run,   "debug",   TASK_STACK_DEBUG,   TASK_PRIO_DEBUG   },
};

/* =========================================================================
 * App_Init —— ICD §22 冻结顺序
 * ====================================================================== */
RC_Result App_Init(void)
{
    RC_Result rc;
    uint32_t i;

    for (i = 0U; i < (uint32_t)APP_TASK_COUNT; ++i) {
        s_handles[i] = 0;
    }

    /* ---- 1. BSP Init ---------------------------------------------------- */
    BSP_UART_Init(RC_DEBUG_UART_BAUD);

    /* ---- 2. MotorManager_Init ------------------------------------------ */
    MotorManager_Init();

    /* ---- 3. PID_Init ----------------------------------------------------
     * 放在 MotorManager 之后，是为了让"先有被控对象的数据所有权，
     * 再有控制器"这件事在启动顺序里也成立。 */
    TaskControl_InitController();

    /* ---- 4. VirtualMotor_Init ------------------------------------------ */
    TaskMotor_InitModel();

    /* ---- 5. CommandQueue Create ---------------------------------------- */
    RC_Port_CmdQueueCreate();

    /* ---- 6. 服务层内部状态 ---------------------------------------------- */
    CommandService_Init();
    MonitorService_Init();

    /* ---- 7. INIT -> READY（ICD §27：启动系统后 state = READY） ---------- */
    rc = MotorManager_SetState(MOTOR_READY);
    if (rc != RC_OK) {
        return rc;
    }

    BSP_UART_WriteLine("=== " RC_VERSION_STRING " | mode=SIMULATION | boot OK ===");
    BSP_UART_WriteLine("commands: START | STOP | SET_SPEED <v> | RESET | STATUS");

    return RC_OK;
}

/* =========================================================================
 * App_CreateTasks
 * ====================================================================== */
RC_Result App_CreateTasks(void)
{
    uint32_t i;

    for (i = 0U; i < (uint32_t)APP_TASK_COUNT; ++i) {
        const BaseType_t ok = xTaskCreate(s_task_desc[i].fn,
                                          s_task_desc[i].name,
                                          s_task_desc[i].stack,
                                          0,
                                          s_task_desc[i].prio,
                                          &s_handles[i]);
        if (ok != pdPASS) {
            /* 任务创建失败属于致命错误：系统会缺少某个职责，
             * 绝不能让启动"看起来成功了"。 */
            s_handles[i] = 0;
            return RC_ERROR;
        }
    }

    return RC_OK;
}

/* =========================================================================
 * App_GetTaskHandle
 * ====================================================================== */
TaskHandle_t App_GetTaskHandle(App_TaskId id)
{
    if ((id < 0) || (id >= APP_TASK_COUNT)) {
        return 0;
    }
    return s_handles[id];
}
