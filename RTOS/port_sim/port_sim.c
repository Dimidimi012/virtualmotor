/**
 * @file    port_sim.c
 * @brief   主机仿真 RTOS 实现 + RC_Port_* 平台原语实现
 *
 * ============================================================================
 * 【为什么用「纤程 (fiber)」而不是普通函数调用，也不是真实线程】
 *
 * 第一版实现是朴素的「单线程 + 函数调用」：调度器直接调用任务的函数体，
 * 期望 vTaskDelay 返回后就等于任务让出了 CPU。这是错的 ——
 * vTaskDelay 是一次普通函数调用，它 return 之后执行流还在任务自己的
 * for(;;) 循环里，于是任务永远不会回到调度器，仿真时钟停在 tick=0。
 * （现场复现：minimal probe 打印 "starting scheduler" 后挂死。）
 *
 * 结论：任务需要一个「能被挂起、之后还能从原处继续」的执行上下文，
 * 也就是协程。可选方案：
 *   1) 真实线程 + 严格乒乓交接  —— 可行，但平台代码翻倍；
 *   2) POSIX ucontext           —— MinGW 不提供，Windows 上不可用；
 *   3) Windows Fiber            —— 单线程、显式切换、每个任务独立栈。
 * 本文件选 3。
 *
 * 纤程的关键性质正好是仿真需要的：
 *   - 只有一次一个执行流在跑 → 结果完全确定、可逐拍复现；
 *   - 切换点只有任务自己调用的阻塞 API → 语义等同于"协作式调度"；
 *   - 每个任务有独立栈 → 任务里的局部变量（如 128 字节的格式化缓冲、
 *     行缓冲下标）在阻塞前后都能正确存活，与真实 FreeRTOS 一致。
 *
 * 【已知边界 —— 必须知道】
 *   协作式调度意味着：任务必须在每一轮循环里至少阻塞一次。
 *   若某个任务永不阻塞，仿真会挂死（真实 FreeRTOS 上表现为该任务
 *   饿死所有低优先级任务，10 ms 控制周期直接失效）。
 *   本文件用看门狗线程把这种情况变成一条明确的报错，而不是静默卡住。
 *
 * 【平台】
 *   当前只有 Windows 后端。移植到 Linux 只需把这一段换成
 *   ucontext(makecontext/swapcontext)，调度逻辑本身不用改。
 * ============================================================================
 */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "sim_rtos.h"

#include "rc_port.h"
#include "config.h"

#include <string.h>
#include <stdio.h>

#if !defined(_WIN32)
#error "port_sim currently has a Windows (fiber) backend only. See file header for the ucontext port."
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma GCC diagnostic pop

#define SIM_MAX_TASKS          (8)
#define SIM_MAX_QUEUES         (4)
#define SIM_MAX_QUEUE_ITEMS    (16)
#define SIM_MAX_ITEM_SIZE      (32)
#define SIM_TASK_STACK_BYTES   (128U * 1024U)
#define SIM_WATCHDOG_TIMEOUT_MS (20000U)

/* -------------------------------------------------------------------------
 * 任务表
 * ---------------------------------------------------------------------- */
typedef struct {
    bool           used;
    bool           blocked;      /* 正在等 wake_tick */
    bool           suspended;
    bool           finished;
    TaskFunction_t fn;
    const char    *name;
    void          *param;
    UBaseType_t    prio;
    TickType_t     wake_tick;
    void          *fiber;        /* LPVOID */
} SimTask;

typedef struct {
    bool        used;
    UBaseType_t length;
    UBaseType_t item_size;
    UBaseType_t count;
    UBaseType_t head;
    uint8_t     storage[SIM_MAX_QUEUE_ITEMS][SIM_MAX_ITEM_SIZE];
} SimQueue;

static SimTask  s_tasks[SIM_MAX_TASKS];
static SimQueue s_queues[SIM_MAX_QUEUES];

static TickType_t s_tick;
static TickType_t s_tick_limit;
static int        s_current          = -1;
static bool       s_scheduler_running;
static void      *s_scheduler_fiber;   /* LPVOID：调度器所在纤程 */

static volatile long s_heartbeat;      /* 看门狗心跳 */
static volatile long s_scheduler_active;
static HANDLE        s_watchdog_thread;

static QueueHandle_t s_cmd_queue;

/* =========================================================================
 * 纤程切换
 * ====================================================================== */
static void sim_scheduler_yield(void)
{
    if (s_scheduler_fiber != 0) {
        SwitchToFiber(s_scheduler_fiber);
    }
}

/** @brief 任务入口：在任务自己的栈上运行，返回即代表任务结束 */
static void CALLBACK sim_task_trampoline(void *param)
{
    SimTask *t = (SimTask *)param;

    if ((t != 0) && (t->fn != 0)) {
        t->fn(t->param);
    }

    /* 任务函数本应死循环；一旦返回说明它结束了 */
    if (t != 0) {
        t->finished = true;
    }
    sim_scheduler_yield();
}

/* =========================================================================
 * 看门狗：把"任务不阻塞导致仿真挂死"变成一条明确报错
 * ====================================================================== */
static DWORD WINAPI sim_watchdog_proc(LPVOID unused)
{
    long last = -1;
    int  stuck_ms = 0;

    (void)unused;

    for (;;) {
        Sleep(200U);

        /* 只有在调度器真正运行期间才判定"卡住"：
         * 测试程序在两次启动调度器之间会跑很久的普通代码，
         * 那段时间心跳本来就不动，不能算故障。 */
        if (s_scheduler_active == 0L) {
            last     = s_heartbeat;
            stuck_ms = 0;
            continue;
        }

        if (s_heartbeat == last) {
            stuck_ms += 200;
            if (stuck_ms >= (int)SIM_WATCHDOG_TIMEOUT_MS) {
                fprintf(stderr,
                        "[sim] FATAL: scheduler made no progress for %d ms "
                        "(tick=%lu). A task is not blocking.\n",
                        stuck_ms, (unsigned long)s_tick);
                fflush(stderr);
                ExitProcess(2U);
            }
        } else {
            last = s_heartbeat;
            stuck_ms = 0;
        }
    }
}

/* =========================================================================
 * 测试台控制（sim_rtos.h）
 * ====================================================================== */
void sim_rtos_set_tick_limit(TickType_t ticks)
{
    s_tick_limit = ticks;
}

TickType_t sim_rtos_get_tick(void)
{
    return s_tick;
}

int sim_rtos_in_task(void)
{
    return (s_current >= 0) ? 1 : 0;
}

void sim_rtos_advance_tick(TickType_t ticks)
{
    s_tick += ticks;
}

void sim_rtos_reset(void)
{
    int i;

    for (i = 0; i < SIM_MAX_TASKS; ++i) {
        if (s_tasks[i].fiber != 0) {
            DeleteFiber(s_tasks[i].fiber);
        }
    }

    memset(s_tasks, 0, sizeof(s_tasks));
    memset(s_queues, 0, sizeof(s_queues));

    s_tick              = 0U;
    s_tick_limit        = 0U;
    s_current           = -1;
    s_scheduler_running = false;
    s_cmd_queue         = 0;
    s_scheduler_fiber   = 0;
    s_heartbeat         = 0L;
}

/* =========================================================================
 * RC_Port_* —— 平台原语
 * ====================================================================== */
uint32_t RC_Port_GetTick(void)
{
    return (uint32_t)s_tick;
}

void RC_Port_EnterCritical(void)
{
    /* 仿真中同一时刻只有一个执行流在跑，天然互斥。
     * 保留空实现是为了让调用点与真实平台完全一致。 */
}

void RC_Port_ExitCritical(void)
{
}

void RC_Port_CmdQueueCreate(void)
{
    s_cmd_queue = xQueueCreate((UBaseType_t)RC_CMD_QUEUE_LENGTH,
                               (UBaseType_t)sizeof(CommandMessage));
}

RC_Result RC_Port_CmdQueueSend(const CommandMessage *msg)
{
    if ((s_cmd_queue == 0) || (msg == 0)) {
        return RC_ERROR;
    }
    if (xQueueSend(s_cmd_queue, msg, (TickType_t)RC_CMD_QUEUE_SEND_TIMEOUT_TICKS) != pdTRUE) {
        return RC_BUSY;
    }
    return RC_OK;
}

RC_Result RC_Port_CmdQueueReceive(CommandMessage *msg)
{
    if ((s_cmd_queue == 0) || (msg == 0)) {
        return RC_ERROR;
    }
    if (xQueueReceive(s_cmd_queue, msg, (TickType_t)0) != pdTRUE) {
        return RC_TIMEOUT;
    }
    return RC_OK;
}

/* =========================================================================
 * 任务 API
 * ====================================================================== */
BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char    *pcName,
                       uint16_t       usStackDepth,
                       void          *pvParameters,
                       UBaseType_t    uxPriority,
                       TaskHandle_t  *pxCreatedTask)
{
    int i;

    (void)usStackDepth;   /* 仿真统一使用固定栈；参数保留以匹配真实 FreeRTOS 签名 */

    for (i = 0; i < SIM_MAX_TASKS; ++i) {
        if (!s_tasks[i].used) {
            s_tasks[i].used      = true;
            s_tasks[i].blocked   = false;
            s_tasks[i].suspended = false;
            s_tasks[i].finished  = false;
            s_tasks[i].fn        = pxTaskCode;
            s_tasks[i].name      = pcName;
            s_tasks[i].param     = pvParameters;
            s_tasks[i].prio      = uxPriority;
            s_tasks[i].wake_tick = 0U;
            s_tasks[i].fiber     = 0;   /* 纤程在调度器启动时创建 */

            if (pxCreatedTask != 0) {
                *pxCreatedTask = (TaskHandle_t)&s_tasks[i];
            }
            return pdPASS;
        }
    }

    return pdFAIL;
}

void vTaskDelay(TickType_t xTicksToDelay)
{
    if (s_current < 0) {
        return;   /* 调度器未运行：等价于 no-op */
    }

    if (xTicksToDelay == 0U) {
        xTicksToDelay = 1U;   /* 与真实 FreeRTOS 一致：delay(0) 仍然让出一次 */
    }

    s_tasks[s_current].wake_tick = s_tick + xTicksToDelay;
    s_tasks[s_current].blocked   = true;
    sim_scheduler_yield();        /* 关键：切回调度器，而不是 return 到任务循环里 */
}

void vTaskDelayUntil(TickType_t *pxPreviousWakeTime, TickType_t xTimeIncrement)
{
    TickType_t next;

    if ((s_current < 0) || (pxPreviousWakeTime == 0)) {
        return;
    }

    next = *pxPreviousWakeTime + xTimeIncrement;

    /* 已经错过截止时刻：不追赶、不补跑。真实 FreeRTOS 会保留欠账，
     * 仿真选择重新对齐，以便始终产出严格等间隔的曲线，便于比较数据。 */
    if ((int32_t)(next - s_tick) <= 0) {
        next = s_tick + xTimeIncrement;
    }

    *pxPreviousWakeTime = next;
    s_tasks[s_current].wake_tick = next;
    s_tasks[s_current].blocked   = true;
    sim_scheduler_yield();
}

void vTaskSuspend(TaskHandle_t xTaskToSuspend)
{
    SimTask *t = (SimTask *)xTaskToSuspend;
    bool     is_current;

    if (t == 0) {
        return;
    }

    is_current = (&s_tasks[s_current] == t) && (s_current >= 0);
    t->suspended = true;
    t->blocked   = true;

    if (is_current) {
        sim_scheduler_yield();
    }
}

void vTaskResume(TaskHandle_t xTaskToResume)
{
    SimTask *t = (SimTask *)xTaskToResume;

    if (t == 0) {
        return;
    }
    t->suspended = false;
    t->blocked   = false;
}

TickType_t xTaskGetTickCount(void)
{
    return s_tick;
}

void vTaskDelete(TaskHandle_t xTaskToDelete)
{
    SimTask *t = (SimTask *)xTaskToDelete;

    if (t == 0) {
        return;
    }
    t->used     = false;
    t->finished = true;
    t->blocked  = true;
}

/* =========================================================================
 * 调度器
 * ====================================================================== */
void vTaskStartScheduler(void)
{
    int  i;
    bool wd_started = false;

    if (s_scheduler_running) {
        return;
    }
    s_scheduler_running = true;

    s_scheduler_active = 1L;
    s_scheduler_fiber = ConvertThreadToFiber(0);
    if (s_scheduler_fiber == 0) {
        s_scheduler_active = 0L;
        fprintf(stderr, "[sim] FATAL: ConvertThreadToFiber failed\n");
        s_scheduler_running = false;
        return;
    }

    for (i = 0; i < SIM_MAX_TASKS; ++i) {
        if (s_tasks[i].used) {
            s_tasks[i].blocked   = false;
            s_tasks[i].wake_tick = s_tick;
            s_tasks[i].fiber     = CreateFiber(SIM_TASK_STACK_BYTES,
                                               sim_task_trampoline,
                                               &s_tasks[i]);
            if (s_tasks[i].fiber == 0) {
                fprintf(stderr, "[sim] FATAL: CreateFiber failed for '%s'\n",
                        (s_tasks[i].name != 0) ? s_tasks[i].name : "?");
                s_scheduler_active  = 0L;
                s_scheduler_running = false;
                return;
            }
        }
    }

    /* 看门狗：把"任务不阻塞"从"永远卡住"变成一条有时间戳的报错 */
    if (s_watchdog_thread == 0) {
        s_watchdog_thread = CreateThread(0, 0, sim_watchdog_proc, 0, 0, 0);
        wd_started = (s_watchdog_thread != 0);
    }
    (void)wd_started;

    for (;;) {
        int best = -1;

        ++s_heartbeat;

        if ((s_tick_limit != 0U) && (s_tick >= s_tick_limit)) {
            break;
        }

        /* 1) 唤醒到期任务 */
        for (i = 0; i < SIM_MAX_TASKS; ++i) {
            if (s_tasks[i].used && !s_tasks[i].suspended && s_tasks[i].blocked) {
                if ((int32_t)(s_tick - s_tasks[i].wake_tick) >= 0) {
                    s_tasks[i].blocked = false;
                }
            }
        }

        /* 2) 选优先级最高者；同优先级取创建顺序靠前者（确定性） */
        for (i = 0; i < SIM_MAX_TASKS; ++i) {
            if (s_tasks[i].used && !s_tasks[i].blocked && !s_tasks[i].suspended) {
                if ((best < 0) || (s_tasks[i].prio > s_tasks[best].prio)) {
                    best = i;
                }
            }
        }

        /* 3) 无人就绪 -> 推进时间 */
        if (best < 0) {
            ++s_tick;
            continue;
        }

        /* 4) 运行直到该任务主动阻塞（或结束） */
        s_current = best;
        SwitchToFiber(s_tasks[best].fiber);

        if (s_tasks[best].finished) {
            DeleteFiber(s_tasks[best].fiber);
            s_tasks[best].fiber = 0;
            s_tasks[best].used  = false;
        }

        s_current = -1;
    }

    /* 收尾：删除任务纤程，并把本线程还原成普通线程 */
    for (i = 0; i < SIM_MAX_TASKS; ++i) {
        if (s_tasks[i].fiber != 0) {
            DeleteFiber(s_tasks[i].fiber);
            s_tasks[i].fiber = 0;
        }
        s_tasks[i].used = false;
    }

    ConvertFiberToThread();
    s_scheduler_active  = 0L;
    s_scheduler_fiber   = 0;
    s_scheduler_running = false;
    s_tick_limit        = 0U;
}

/* =========================================================================
 * 队列 API（按值拷贝，与真实 FreeRTOS 语义一致）
 * ====================================================================== */
QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
    int i;

    if ((uxQueueLength == 0U) || (uxItemSize == 0U) ||
        (uxQueueLength > SIM_MAX_QUEUE_ITEMS) || (uxItemSize > SIM_MAX_ITEM_SIZE)) {
        return 0;
    }

    for (i = 0; i < SIM_MAX_QUEUES; ++i) {
        if (!s_queues[i].used) {
            s_queues[i].used      = true;
            s_queues[i].length    = uxQueueLength;
            s_queues[i].item_size = uxItemSize;
            s_queues[i].count     = 0U;
            s_queues[i].head      = 0U;
            return (QueueHandle_t)&s_queues[i];
        }
    }

    return 0;
}

BaseType_t xQueueSend(QueueHandle_t xQueue,
                      const void   *pvItemToQueue,
                      TickType_t    xTicksToWait)
{
    SimQueue  *q = (SimQueue *)xQueue;
    TickType_t deadline;
    UBaseType_t tail;

    if ((q == 0) || (pvItemToQueue == 0)) {
        return pdFALSE;
    }

    deadline = s_tick + xTicksToWait;

    for (;;) {
        if (q->count < q->length) {
            tail = (q->head + q->count) % q->length;
            memcpy(q->storage[tail], pvItemToQueue, (size_t)q->item_size);
            ++q->count;
            return pdTRUE;
        }

        if (xTicksToWait == 0U) {
            return errQUEUE_FULL;     /* ICD §15：不得无限阻塞 */
        }
        if ((int32_t)(s_tick - deadline) >= 0) {
            return errQUEUE_FULL;
        }

        vTaskDelay(1U);
    }
}

BaseType_t xQueueReceive(QueueHandle_t xQueue,
                         void         *pvBuffer,
                         TickType_t    xTicksToWait)
{
    SimQueue  *q = (SimQueue *)xQueue;
    TickType_t deadline;

    if ((q == 0) || (pvBuffer == 0)) {
        return pdFALSE;
    }

    deadline = s_tick + xTicksToWait;

    for (;;) {
        if (q->count > 0U) {
            memcpy(pvBuffer, q->storage[q->head], (size_t)q->item_size);
            q->head = (q->head + 1U) % q->length;
            --q->count;
            return pdTRUE;
        }

        if (xTicksToWait == 0U) {
            return pdFALSE;
        }
        if ((int32_t)(s_tick - deadline) >= 0) {
            return pdFALSE;
        }

        vTaskDelay(1U);
    }
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue)
{
    SimQueue *q = (SimQueue *)xQueue;
    return (q == 0) ? 0U : q->count;
}
