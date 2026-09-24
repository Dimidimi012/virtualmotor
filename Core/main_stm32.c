/**
 * @file    main_stm32.c
 * @brief   STM32 硬件入口 —— 对应 ICD V1.0 第 22 节「启动顺序（冻结）」
 *
 * 与主机仿真入口 Core/main.c 的关系：
 *   两者调用完全相同的 App_Init() / App_CreateTasks() / vTaskStartScheduler()。
 *   区别只有一处：硬件入口在 App_Init 之前多出 HAL / Clock / 外设初始化，
 *   这正是 ICD §22 顺序图里那句 "HAL / Clock Init"。
 *
 * 换句话说：换到硬件时，唯一新增的是"平台启动"，控制逻辑一行都不变。
 *
 * 【验证状态】
 *   未在当前环境编译验证（缺少 arm-none-eabi 工具链 / HAL / FreeRTOS-Kernel）。
 *   见 docs/BUILD_STM32.md。
 *
 * ============================================================================
 * 时钟树（先把这张图写下来，再写第一行配置代码）
 *
 *   HSE 8 MHz
 *     -> PLL (M=8, N=336, P=2, Q=7)
 *     -> SYSCLK 168 MHz
 *   AHB  /1 -> HCLK   168 MHz   (Cortex-M4F + FPU + Flash 5WS)
 *   APB1 /4 -> PCLK1   42 MHz   -> APB1 定时器时钟 = 84 MHz  (预分频 != 1 时 x2)
 *   APB2 /2 -> PCLK2   84 MHz   -> APB2 定时器时钟 = 168 MHz (同上)
 *
 * 这张图的用途：
 *   - CAN 位时序来自 APB1，波特率算错多半是因为忘了那个 x2 规则；
 *   - 任何"定时器频率对不上"的问题，都可以先回到这张图定位；
 *   - configCPU_CLOCK_HZ 必须与 SYSCLK 一致，否则 FreeRTOS tick 不是 1 ms。
 *
 * 中断优先级规划（数值越小优先级越高；见 ICD §16 与 docs/BUILD_STM32.md）
 *   | 中断                     | 抢占优先级 | 理由                               |
 *   |--------------------------|-----------|------------------------------------|
 *   | USART2 (调试串口 RX)      | 5         | 必须 >= configLIBRARY_MAX_SYSCALL_ |
 *   |                          |           | INTERRUPT_PRIORITY 才能用 FromISR  |
 *   | SysTick / PendSV         | 15        | FreeRTOS 内核占用，不要动           |
 * ============================================================================
 */

#include "stm32f4xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app.h"
#include "config.h"
#include "types.h"
#include "bsp_uart.h"

/* CubeMX 生成的外设句柄（bsp_uart_stm32.c 通过 extern 引用） */
UART_HandleTypeDef huart2;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);

/* =========================================================================
 * FreeRTOS 钩子
 * ====================================================================== */

/** ICD §21：不用断言做运行时保护；但这个钩子属于"启动期不可恢复错误"，
 *  必须让它可见，而不是让系统带着一个失败的分配继续跑。 */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {
        /* 现场排查提示：先看是不是 configTOTAL_HEAP_SIZE 太小，
         * 再看是不是某个任务的栈深度设置过大。 */
    }
}

/** 栈溢出钩子（configCHECK_FOR_STACK_OVERFLOW = 2 时有效） */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

/** configASSERT 的落地实现 */
void RC_Port_AssertFailed(const char *file, int line)
{
    (void)file;
    (void)line;
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

/* =========================================================================
 * main —— 严格按 ICD §22 的顺序
 * ====================================================================== */
int main(void)
{
    /* ---- HAL / Clock Init ------------------------------------------------ */
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();

    /* ---- BSP Init -> MotorManager_Init -> PID_Init -> VirtualMotor_Init ->
     *      CommandQueue Create -> 服务层初始化 -> INIT -> READY ---------- */
    if (App_Init() != RC_OK) {
        Error_Handler();
    }

    /* ---- Create Tasks ---------------------------------------------------- */
    if (App_CreateTasks() != RC_OK) {
        Error_Handler();
    }

    /* ---- vTaskStartScheduler（永不返回） --------------------------------- */
    vTaskStartScheduler();

    /* 只有调度器启动失败（例如堆不足）才会走到这里 */
    Error_Handler();
    return 0;
}

/* =========================================================================
 * 以下为 CubeMX 生成内容（保持原样，不要手改）
 * ====================================================================== */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* HSE 8 MHz -> PLL -> 168 MHz */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState            = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM            = 8U;
    RCC_OscInitStruct.PLL.PLLN            = 336U;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ            = 7U;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* HCLK  = 168 MHz */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;     /* PCLK1 =  42 MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;     /* PCLK2 =  84 MHz */
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = RC_DEBUG_UART_BAUD;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;) {
    }
}
