# STM32 Hardware Mode 构建与移植指南

> ICD §0：**运行模式默认 Simulation Mode；预留 Hardware Mode。**
> 本文说明"预留"具体预留了什么、怎么落地、以及哪些地方最容易出错。

---

## 1. 现在预留了什么

| 层 | 文件 | 状态 |
|---|---|---|
| 平台原语 | `RTOS/port_freertos/port_freertos.c` | 已按接口写全，**未编译验证** |
| 串口 BSP | `BSP/bsp_uart_stm32.c` | 已按 HAL 接口写全，**未编译验证** |
| 内核配置 | `Core/FreeRTOSConfig.h` | 已写全，**未编译验证** |
| 程序入口 | `Core/main_stm32.c` | 已写全（含时钟树注释与 CubeMX 段），**未编译验证** |
| 构建脚本 | `build/Makefile.stm32` | 已写全，**未编译验证** |

**为什么"未验证"也要写出来**：
ICD 的核心思想是"先锁接口，再写实现"。把端口层的接口与实现骨架先定下来，
换硬件时才不会出现"每个模块都对，但拼不起来"。同时，未验证的部分必须**如实标注** ——
把没跑过的代码说成跑过了，是这类交付里最严重的错误。

**验证状态：本机没有 `arm-none-eabi` 工具链，也没有 STM32 HAL / CMSIS / FreeRTOS-Kernel 源码。**
按下面第 4 节的步骤，在真实工程里跑通一次即可转正。

---

## 2. 需要准备的依赖

```
RoboControl/                    <- 本仓库
deps/
├── FreeRTOS-Kernel/            <- v11.1.0 或兼容版本
│   ├── include/
│   ├── portable/GCC/ARM_CM4F/  <- cortex-m4f 端口
│   └── portable/MemMang/heap_4.c
└── STM32CubeF4/Drivers/        <- 或用 CubeMX 生成的 Drivers 目录
    ├── STM32F4xx_HAL_Driver/
    └── CMSIS/
```

工具链：`arm-none-eabi-gcc`（建议 12 以上）、`make`、`st-flash` 或 ST-LINK Utility。

---

## 3. 构建

```bash
export FREERTOS_DIR=/path/to/FreeRTOS-Kernel
export HAL_DIR=/path/to/STM32CubeF4/Drivers
export LDSCRIPT=/path/to/STM32F407ZGTx_FLASH.ld

make -f build/Makefile.stm32
make -f build/Makefile.stm32 size
make -f build/Makefile.stm32 flash
```

**移植时必改的四处**（都已在代码里用 `TODO(移植)` 标出）：

| 位置 | 改什么 |
|---|---|
| `build/Makefile.stm32` | `-mcpu` / `-DSTM32F407xx` / 链接脚本 / startup 文件名 |
| `Core/FreeRTOSConfig.h` | `configCPU_CLOCK_HZ` 必须等于实际 SYSCLK |
| `BSP/bsp_uart_stm32.c` | `BSP_UART_HANDLE` / `BSP_UART_IRQn` |
| `Core/main_stm32.c` | 时钟树数值（PLLM/N/P/Q）与 USART 实例 |

---

## 4. 验收步骤（按顺序做，每步都要有观测）

> 原则：**先能被观测，再往下走。** 每一步都要能回答"我怎么知道它对了"。

| 步骤 | 做什么 | 怎么确认 |
|---|---|---|
| 1 | 编译通过，看 `size` | Flash / RAM 占用打印出来，RAM 要留出 `configTOTAL_HEAP_SIZE` 之外的余量 |
| 2 | 只跑 HAL + 串口裸发 | 串口助手能收到一行文本（先不用 FreeRTOS） |
| 3 | 上 FreeRTOS，只跑 DebugTask | 每 500 ms 收到一行 `TGT=...`，**用示波器或连续时间戳确认周期真的是 500 ms** |
| 4 | 上全部 5 个任务 | 观察 10 ms 周期的抖动；若抖动大，先查中断优先级 |
| 5 | 键入 `START` / `SET_SPEED 1000` | 实际速度按曲线逼近目标（与仿真形状一致） |
| 6 | 拔掉串口再插回 | 系统不崩，命令可继续（验证不阻塞在 UART 上） |
| 7 | 人为让 MotorTask 停跑 | 100 ms 内转 ERROR，输出置零 |

---

## 5. 中断优先级表（写进工程，不要随手写）

Cortex-M4 用 4 位优先级，**数值越小优先级越高**。

| 中断 | 抢占优先级 | 理由 |
|---|---|---|
| USART2（调试串口 RX） | 5 | 必须 `>= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`(5)，否则 `vTaskNotifyGiveFromISR` 会破坏内核临界区 |
| SysTick / PendSV | 15 | FreeRTOS 内核占用，**不要改** |
| （未来）CAN RX | 1 | 数据新鲜度决定环路上限 |
| （未来）电流环定时器 | 0 | 最高硬实时 |

两条纪律：
① 同一抢占优先级的中断之间不能互相等待；
② **不要在 ISR 里等信号量/互斥量**。

---

## 6. 已知的坑（本项目相关的）

### 6.1 `%f` 打印不出来

newlib-nano 默认不链接浮点格式化。现象是输出里 `%.1f` 变成空白或乱码。
解决：链接选项加 `-u _printf_float`（本工程的 Makefile 已在 `LDFLAGS` 里预留位置）。
**记在这里的原因**：这是"代码没错但什么都看不到"的典型，第一次遇到会排查很久。

### 6.2 SVC_Handler / PendSV_Handler / SysTick_Handler 重复定义

`Core/FreeRTOSConfig.h` 里做了 `#define xPortSysTickHandler SysTick_Handler` 之类的映射，
此时必须把 `Core/stm32f4xx_it.c` 里 CubeMX 生成的同名函数**删掉**。
两者选其一，不能同时存在 —— 这是 FreeRTOS + CubeMX 最常见的链接错误。

### 6.3 中断优先级与 FromISR API

任何调用 `xxxFromISR` 的中断，其**抢占优先级数值**必须
`>= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`。
写错的现象是"偶发死机"，而且很难复现。本工程把这条约束写进了
`bsp_uart_stm32.c` 的注释和上面的优先级表。

### 6.4 NVIC 优先级分组的陷阱

HAL 默认把优先级分组设为 4 位抢占 / 0 位子优先级（`NVIC_PRIORITYGROUP_4`）。
如果被改成别的分组，上面那张表就不再成立 —— 数值算出来的实际抢占级别会变。
换 CubeMX 配置时留意这一项。

### 6.5 `configCPU_CLOCK_HZ` 与实际时钟不一致

现象是"控制环好像有点慢"，但因为一切都能跑，很难联想到 tick 周期。
本工程在 `main_stm32.c` 顶部写了完整时钟树注释，就是为了让这个值有据可查。

### 6.6 `vTaskDelayUntil` 的重新对齐策略

主机仿真里，如果错过截止时刻，仿真器**重新对齐**而不是补跑（为了产出严格等间隔的曲线）。
真实 FreeRTOS 会保留欠账。这个差异只影响"长时间被抢占"的场景，
不影响正常运行的周期精度，但值得知道。

---

## 7. 与主机仿真的一致性保证

主机与硬件共用**同一份**下列文件，无需任何 `#ifdef`：

```
Control/pid.c              Simulation/virtual_motor.c
Service/motor_manager.c    Service/command_service.c   Service/monitor_service.c
RTOS/task_control.c        RTOS/task_motor.c           RTOS/task_command.c
RTOS/task_monitor.c        RTOS/task_debug.c           Application/app.c
```

唯一替换的是：

| 主机仿真 | STM32 硬件 |
|---|---|
| `RTOS/port_sim/port_sim.c` | `RTOS/port_freertos/port_freertos.c` |
| `BSP/bsp_uart_sim.c` | `BSP/bsp_uart_stm32.c` |
| `RTOS/port_sim/FreeRTOS.h`（兼容子集） | FreeRTOS-Kernel 的真实头文件 |
| `Core/main.c` | `Core/main_stm32.c` |

任务优先级也来自同一处（`Common/config.h` 的 `TASK_PRIO_*`），
仿真侧 `FreeRTOS.h` 与硬件侧 `FreeRTOSConfig.h` 都 include 它 ——
因此"仿真里跑的任务优先级等于硬件上的优先级"是**结构保证**，不是口头约定。
