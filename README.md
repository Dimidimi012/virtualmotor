# RoboControl V1.0

> STM32F4 + FreeRTOS + PID + Virtual Motor 的电控闭环软件骨架


---

## 1. 这个项目是什么

一句话：**没有真实电机，也要把控制闭环和软件架构完整地做出来、跑起来、讲明白。**

它交付的不是"一段能转电机的代码"，而是一套边界清晰、可编译、可仿真、可展示的工程骨架：

```
UART 命令  ->  CommandTask  ->  CommandQueue  ->  ControlTask  ->  PID
                                                     ^              |
                                                     |              v
                        Motor Feedback  <-  MotorTask  <-  VirtualMotor
                                |
                                v
                     MonitorTask (保护)  /  DebugTask (观测)
```

四个"能"：

| 目标 | 如何达成 |
|---|---|
| **可编译** | 主机端一条命令构建；产品代码零警告通过 `-Wall -Wextra -Wconversion -Wsign-conversion` |
| **可仿真** | 主机仿真运行**真实的 5 个 FreeRTOS 任务与真实的消息队列**，不是把函数按顺序调一遍 |
| **可验证** | 322 项自动化检查（单元 + 端到端集成），一条命令给出 PASS/FAIL |
| **可展示** | 演示程序自动产出遥测 CSV，工具脚本自动算出阶跃响应指标并绘出 SVG |

---

## 2. 一分钟跑起来

依赖：**任意 C99 编译器**（gcc / clang / `py -3 -m ziglang cc`）。没有编译器时：

```powershell
pip install ziglang      # 自带完整 C 工具链，无需管理员权限
```

然后：

```powershell
cd RoboControl

# 1) 构建并运行全部测试（322 项检查）
powershell -ExecutionPolicy Bypass -File build\build_sim.ps1 -Target tests
.\build\tests.exe

# 2) 构建并运行演示（正常场景 / 故障注入场景）
powershell -ExecutionPolicy Bypass -File build\build_sim.ps1 -Target demo
.\build\sim_demo.exe normal docs\data\trace_normal.csv
.\build\sim_demo.exe fault  docs\data\trace_fault.csv

# 3) 把原始遥测变成可写进报告的指标与曲线
python tools\analyze_trace.py docs\data\trace_normal.csv docs\data\trace_fault.csv ^
       --svg docs\data\step_response.svg --md docs\data\TEST_DATA.md
```

### 3.1 自动化测试

```
TOTAL: 322 checks, 0 failures
RESULT: PASS
```

覆盖范围与 ICD §28 阶段的对应关系：

| 套件 | 对应 ICD | 检查数 |
|---|---|---|
| Config | §20 配置不变量、§12 优先级、§16 阈值 | 38 |
| PID | §6 接口 / §7 算法冻结规则 / §21 错误策略 | 71 |
| VirtualMotor | §8 模型与半隐式欧拉顺序 | 92 |
| MotorManager | §4 数据所有权 / §9 API / §10 状态机 | 175 |
| CommandService | §11 API / §15 命令集 / §21 错误码 | 252 |
| MonitorService | §16 基础保护 | 271 |
| ClosedLoop | §27 端到端验收（真实调度器 + 真实任务） | 322 |

### 3.2 闭环阶跃响应（由 tools/analyze_trace.py 从原始 CSV 自动算出）

| 阶跃 | 超调 (%) | 上升时间 (ms) | 整定时间 (ms) | 稳态误差 |
|---|---|---|---|---|
| 100 -> 1000 | 16.11 | 90 | 570 | +0.000 |
| 1080 -> 1500 | 10.88 | 70 | 510 | +0.000 |
| 1370 -> -800 | 14.50 | 170 | 660 | -0.000 |
| 100 -> 600 | 10.72 | 70 | 510 | +0.046 |

**稳态误差为 0**：积分项把静差消干净了。
**超调 11% ~ 16%**：这不是调得不好，而是 ICD §7 冻结的算法**没有条件积分抗饱和**的直接后果 ——
详见 `docs/ICD_CHANGE_REQUEST.md` 的 CR-001，以及 `docs/tuning-log.md` 里那份量化对比。
把"已知缺陷 + 量化证据 + 改进路径"一起交出来，比假装没有缺陷更接近工程真实。

### 3.3 故障注入（ICD §27 第 10 条）

```
t = 4000 ms  [scenario] fault injected: motor feedback line cut
             TGT=1000.0 ACT=1000.0 OUT=0.00 POS=2990.0 STATE=ERROR FAULT=0x00000002
```

反馈中断后 **100 ms 内**（MonitorTask 的一个周期）被检出，输出立即置零，状态转 `ERROR`。

---

## 4. 目录结构

```
RoboControl/
├── Common/          types.h  config.h  rc_port.h        公共类型、集中配置、平台原语声明
├── Control/         pid.h/.c                            纯算法，无 RTOS / 无 HAL
├── Simulation/      virtual_motor.h/.c                  一阶简化动力学模型
├── Service/         motor_manager.h/.c  command_service  monitor_service
├── RTOS/            task_control/motor/command/monitor/debug
│   ├── port_sim/    主机仿真 RTOS（FreeRTOS 兼容子集 + Windows 纤程调度）
│   └── port_freertos/  真实 FreeRTOS 上的平台原语实现（Hardware Mode）
├── BSP/             bsp_uart.h  bsp_uart_sim.c  bsp_uart_stm32.c
├── Application/     app.h/.c                            ICD §22 冻结启动顺序
├── Core/            main.c（主机）  main_stm32.c  FreeRTOSConfig.h（硬件）
├── Sim/             sim_demo.c                          演示与数据采集台
├── Tests/           7 个测试套件
├── tools/           analyze_trace.py                    遥测 -> 指标 + SVG
├── build/           build_sim.ps1  Makefile.stm32
└── docs/            ARCHITECTURE  ICD_CHANGE_REQUEST  tuning-log
                     ACCEPTANCE  PRESENTATION  BUILD_STM32  data/
```

---

## 5. 五条最重要的设计决定

这五条既是实现约束，也是答辩时最值得讲的部分。

**1）单一数据所有权。**
`MotorData` 的唯一存储在 `motor_manager.c` 里，用 `static` 修饰 ——
其他编译单元**即使写 extern 也拿不到**。所有权不是文档里的一句话，而是编译器的约束。

**2）算法层零依赖。**
`Control/@ 与 `Simulation/@ 不 include 任何 FreeRTOS、HAL、UART 头文件，
只用 `float`。因此它们能在 PC 上单独编译并做单元测试 —— 这是唯一能离线验证的部分。

**3）平台差异被压缩到一个 `rc_port.h`。**
RTOS 任务代码在主机和 STM32 上**一字不改**。差异只有三个原语
（取 tick、短临界区、命令队列），由 `port_sim` 或 `port_freertos` 各自实现。

**4）判断与动作分离。**
`MonitorService_Check()` 只返回故障位（可脱离 RTOS 单测），
`MonitorTask` 负责执行 `StopOutput + ERROR`。薄的那一层不可能出错，厚的那一层可以测。

**5）故障分两类。**
只有"**我们已经不知道电机正在发生什么**"这一类的故障才会触发 `StopOutput + ERROR`
（心跳丢失、反馈过期、NaN/Inf、超范围、长期无响应）。
命令层面的故障（队列满、解析失败）**只记录、不停机** ——
在串口上打错一个字就让电机急停，是把可用性问题升级成了安全问题。

---

## 6. 已知边界（如实说明）

| 项 | 现状 |
|---|---|
| 主机仿真是**协作式**调度 | 任务必须每轮阻塞一次；不阻塞会触发看门狗报错而不是静默卡死。真实抢占由 STM32 + FreeRTOS 承担 |
| STM32 端口**未在本机编译验证** | 本机没有 arm-none-eabi 工具链与 HAL/FreeRTOS 源码；文件已按接口写全，见 docs/BUILD_STM32.md 的验收步骤 |
| PID 无抗饱和 | ICD §7 冻结算法所致，已量化为 CR-001，V1.0 不做静默修改 |
| VirtualMotor 是一阶模型 | ICD §8 明确为"演示级简化模型"，不是电机电磁模型 |
| 一次 RESET 不能掩盖仍然存在的故障 | 这是刻意设计：故障源不消失，Monitor 会重新触发 ERROR |

---
