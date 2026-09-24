/**
 * @file    main.c
 * @brief   RoboControl V1.0 程序入口 —— 对应 ICD V1.0 第 22 节「启动顺序（冻结）」
 *
 * ICD §22 冻结顺序：
 *     main()
 *      ↓ HAL / Clock Init      <- 平台相关，见下方注释
 *      ↓ BSP Init
 *      ↓ MotorManager_Init
 *      ↓ PID_Init
 *      ↓ VirtualMotor_Init
 *      ↓ CommandQueue Create
 *      ↓ Create Tasks
 *      ↓ vTaskStartScheduler
 *
 * ICD §22：任何 Task 不得假设其他模块"可能已经初始化"；
 *          初始化顺序由 main/Application 统一负责。
 *
 * 平台差异说明：
 *   主机仿真——没有 HAL/Clock 步骤；且本仿真调度器在到达 tick 上限后会返回，
 *             因此 vTaskStartScheduler 之后的代码在仿真里是可达的。
 *   STM32  ——HAL_Init / SystemClock_Config / MX_*_Init 由 CubeMX 生成，
 *             写在 Core/main_stm32.c 中；那里 vTaskStartScheduler 永不返回。
 */

#include "app.h"
#include "config.h"
#include "types.h"

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_uart.h"

int main(void)
{
    RC_Result rc;

    /* ---- 1/2. HAL / Clock Init + BSP Init（见 App_Init 第 1 步） --------- */
    rc = App_Init();
    if (rc != RC_OK) {
        /* 启动失败必须显式暴露，不能"带病进入调度器"：
         * 一个状态未知的系统比一个不启动的系统更危险。 */
        BSP_UART_WriteLine("BOOT FAILED");
        return 1;
    }

    /* ---- 7. Create Tasks ------------------------------------------------ */
    rc = App_CreateTasks();
    if (rc != RC_OK) {
        BSP_UART_WriteLine("TASK CREATE FAILED");
        return 1;
    }

    /* ---- 8. vTaskStartScheduler ----------------------------------------- */
    vTaskStartScheduler();

    /* 只有主机仿真会走到这里（到达 tick 上限后调度器返回） */
    BSP_UART_WriteLine("scheduler stopped (host simulation tick limit reached)");
    return 0;
}
