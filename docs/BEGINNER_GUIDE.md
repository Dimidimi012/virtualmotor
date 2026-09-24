# 零基础阅读指南（从这里开始）

> **这份文档写给谁**：第一次接触这个项目、没写过嵌入式、看到 PID / FreeRTOS / 临界区
> 就发懵的人。全文不假设你懂任何专业名词 —— 每个词第一次出现时都会用大白话解释。
>
> **如果你时间很紧**，只读第 0、1、3 三节就够跑起来了。
> **如果要讲给别人听**，第 1 节（比喻）、第 5 节（词典）、第 7 节（怎么答）最关键。

---

## 第 0 节 · 先说清楚：东西到底在哪

### 0.1 你的项目在这个路径

```
C:\Users\dimidimi\Desktop\E-control project\
```

打开这个文件夹，你会看到**两样东西**：

| 名字 | 是什么 |
|---|---|
| `RoboControl_V1.0_软件接口定义_ICD_V1.0.docx` | **老师给的任务书**（原始文件，不要动） |
| `RoboControl\` | **我做出来的项目**（所有代码和文档都在这里面） |

**记住一句话：所有要交的东西都在 `RoboControl\` 这个文件夹里。**

### 0.2 项目文件夹里有什么（完整清单，一个不漏）

```
C:\Users\dimidimi\Desktop\E-control project\RoboControl\
│
├── README.md                  ← 【从这里开始看】项目门面：是什么/怎么跑/做成什么样
├── .gitignore                 ← 给 git 用的，忽略编译产物（你不用管）
│
├── Common\                    ← 【第 1 层】公共地基：类型、参数、平台接口
│   ├── types.h                    所有模块共用的数据类型（状态、错误码、命令）
│   ├── config.h                   所有可调参数集中在这一个文件（周期、PID 参数…）
│   └── rc_port.h                  平台接口声明（"我需要读时钟、需要进临界区"）
│
├── Control\                   ← 【第 2 层】纯算法
│   ├── pid.h                      PID 控制器的接口
│   └── pid.c                      PID 控制器的实现（只有 100 行）
│
├── Simulation\                ← 【第 2 层】虚拟电机
│   ├── virtual_motor.h            虚拟电机的接口
│   └── virtual_motor.c            虚拟电机的实现（只有 90 行）
│
├── Service\                   ← 【第 3 层】服务层
│   ├── motor_manager.h/.c         电机数据的管理者（唯一拥有者）
│   ├── command_service.h/.c       命令解析（把 "SET_SPEED 1000" 变成程序能懂的东西）
│   └── monitor_service.h/.c       健康检查（发现异常）
│
├── RTOS\                      ← 【第 4 层】任务层（5 个"厨师"）
│   ├── task_control.h/.c          控制任务：每 10ms 跑一次 PID
│   ├── task_motor.h/.c            电机任务：每 10ms 更新一次虚拟电机
│   ├── task_command.h/.c          命令任务：收串口命令
│   ├── task_monitor.h/.c          监控任务：每 100ms 体检一次
│   ├── task_debug.h/.c            调试任务：每 500ms 打印一行
│   ├── port_sim\                  【电脑上跑】用 Windows 纤程模拟 FreeRTOS
│   └── port_freertos\             【单片机上跑】真实 FreeRTOS 的适配（未验证）
│
├── BSP\                       ← 【第 5 层】板级支持（跟硬件打交道）
│   ├── bsp_uart.h                 串口接口
│   ├── bsp_uart_sim.c             电脑上的串口（用文件/内存假装）
│   └── bsp_uart_stm32.c           单片机的串口（用 HAL 库，未验证）
│
├── Application\               ← 启动流程
│   └── app.h/.c                   按 ICD 规定的顺序把系统启动起来
│
├── Core\                      ← 程序入口
│   ├── main.c                     电脑上跑的入口
│   ├── main_stm32.c               单片机上跑的入口（未验证）
│   └── FreeRTOSConfig.h           单片机上 FreeRTOS 的配置（未验证）
│
├── Sim\                       ← 演示程序
│   └── sim_demo.c                 自动演示：启动→加速→反向→停车→故障
│
├── Tests\                     ← 测试（322 项检查）
│   ├── test_main.c                测试入口
│   ├── test_framework.h/.c        一个 60 行的迷你测试框架
│   ├── test_config.c              检查参数配得对不对
│   ├── test_pid.c                 检查 PID 算得对不对
│   ├── test_virtual_motor.c       检查虚拟电机算得对不对
│   ├── test_motor_manager.c       检查状态机、数据所有权
│   ├── test_command_service.c     检查命令解析
│   ├── test_monitor_service.c     检查故障判定
│   └── test_closed_loop.c         端到端：跑真实的 5 个任务
│
├── tools\                     ← 小工具
│   └── analyze_trace.py           把原始数据算成"超调/上升时间/整定时间"
│
├── build\                     ← 构建脚本和编译产物
│   ├── build_sim.ps1              一条命令编译（Windows）
│   ├── Makefile.stm32             单片机的构建脚本
│   └── tests.exe / sim_demo.exe   编译出来的程序（可以删，重编就有）
│
└── docs\                      ← 【所有文档都在这】
    ├── README.md                  ★ 文档导读：每份文档是干什么的
    ├── BEGINNER_GUIDE.md          ★ 你正在读的这一份
    ├── BUILD_NARRATIVE.md           我是怎么做出来的（答辩用）
    ├── ARCHITECTURE.md              架构设计（读代码用）
    ├── ACCEPTANCE.md                验收对照（证明做完了）
    ├── ICD_CHANGE_REQUEST.md        变更记录（证明没乱改接口）
    ├── tuning-log.md                调参记录（参数依据）
    ├── PRESENTATION.md              答辩讲稿
    ├── BUILD_STM32.md               怎么上真实硬件
    ├── ICD_V1.0_plaintext_extract.txt   任务书的纯文本副本
    └── data\                     实测数据（自动生成，不要手改）
        ├── TEST_DATA.md              指标表
        ├── trace_normal.csv          正常演示的原始数据
        ├── trace_fault.csv           故障演示的原始数据
        ├── step_response.svg         曲线图（可以用浏览器打开）
        ├── fault_response.svg        故障曲线图
        ├── demo_normal_console.txt   正常演示的控制台记录
        └── demo_fault_console.txt    故障演示的控制台记录
```

### 0.3 三个"入口"，按你的目的选

| 你想干什么 | 打开哪一个 |
|---|---|
| **只想把它跑起来看看** | 跳到本文第 3 节，照着敲命令 |
| **想读懂代码** | 本文第 4 节（按顺序带你读每个文件） |
| **想讲给别人听** | 本文第 1、7 节 + `docs/PRESENTATION.md` |
| **想知道每份文档干什么** | `docs/README.md` |

---

## 第 1 节 · 这个项目到底是什么（用最直白的话）

### 1.1 一句话

> **这是一套"没有真电机，也能把电机控制程序完整做出来并验证"的软件。**

### 1.2 打个比方：驾驶模拟器

假设你要学开车，但你现在**没有车**。你有两个选择：

- **选择 A**：等买到车再练。 → 浪费时间，而且车上的毛病和你的驾驶技术会混在一起，很难分辨是谁的问题。
- **选择 B**：先装一个驾驶模拟器，把"怎么看仪表、怎么踩油门、怎么刹车"练熟。 → 等真车来了，你上手就能开。

**这个项目选的就是 B。**

| 真实世界的东西 | 这个项目里对应的东西 | 在哪 |
|---|---|---|
| 真电机 | **虚拟电机**（一段数学公式，算出"如果给它这个电压，转速会变成多少"） | `Simulation/virtual_motor.c` |
| 你踩的油门 | **PID 控制器**（算出"该给多少"） | `Control/pid.c` |
| 仪表盘上的速度 | **反馈**（虚拟电机算出来的当前速度） | `Service/motor_manager.c` |
| 你看着仪表决定踩多深 | **控制任务**（每 10 毫秒做一次这个决定） | `RTOS/task_control.c` |
| 模拟器这台电脑 | **电机任务**（每 10 毫秒算一次物理） | `RTOS/task_motor.c` |
| 副驾驶帮你念路牌 | **命令任务**（从串口收命令） | `RTOS/task_command.c` |
| 安全员 | **监控任务**（发现不对就刹车） | `RTOS/task_monitor.c` |
| 行车记录仪 | **调试任务**（每 500 毫秒记一笔） | `RTOS/task_debug.c` |

### 1.3 数据是怎么流动的（看一眼就懂）

```
你在电脑上敲： SET_SPEED 1000
        │
        ▼
  ① 命令任务收到这行字，翻译成程序能懂的"命令包"
        │
        ▼
  ② 命令包被放进"传送带"（队列）
        │
        ▼
  ③ 控制任务每 10 毫秒从传送带上取一次，看到"目标速度=1000"
        │
        ▼
  ④ 控制任务拿 目标(1000) 和 当前速度(比如 300) 一比，差 700
     交给 PID 算出"该输出多少"，比如 40
        │
        ▼
  ⑤ 控制任务把 40 交给"数据管理者"
        │
        ▼
  ⑥ 电机任务拿到 40，喂给虚拟电机
     虚拟电机算：加速度 = 100×40 - 2×当前速度 → 新速度
        │
        ▼
  ⑦ 电机任务把新速度写回"数据管理者"
        │
        ▼
  ⑧ 回到 ③，10 毫秒后再来一遍 —— 速度就这样一点点逼近 1000
```

**同时**，监控任务每 100 毫秒检查一次"还正常吗"，调试任务每 500 毫秒打印一行结果。

### 1.4 你可以想象的画面

跑起来之后，控制台会一行行打印：

```
TGT=1000.0 ACT=1031.3 OUT=18.38 POS=485.9 STATE=RUNNING FAULT=0x00000000
TGT=1000.0 ACT=1000.8 OUT=19.96 POS=989.9 STATE=RUNNING FAULT=0x00000000
TGT=1000.0 ACT=1000.0 OUT=20.00 POS=1490.0 STATE=RUNNING FAULT=0x00000000
```

读法（**每个字母的含义**）：

| 缩写 | 英文 | 中文 | 上面三行的含义 |
|---|---|---|---|
| TGT | target | 目标速度 | 我要求的速度是 1000 |
| ACT | actual | 实际速度 | 现在是 1031.3 → 1000.8 → 1000.0，**正在收敛** |
| OUT | output | 控制器输出 | PID 说"给 18.38"，然后 19.96，最后稳定在 20.00 |
| POS | position | 位置 | 速度对时间的积分（走了多远） |
| STATE | state | 状态 | RUNNING = 正在运行 |
| FAULT | fault | 故障码 | 0 = 没故障 |

**这就是整个项目最核心的成果**：一个虚拟电机，在你的程序控制下，从静止平稳地加速到 1000 并稳住。

---

## 第 2 节 · 动手之前：先确认你电脑上有没有"翻译官"

### 2.1 为什么需要"翻译官"

你写的是 C 语言代码（人能读），但电脑只懂机器码。
中间需要一个程序把 C 翻译成电脑能执行的程序 —— 这叫**编译器**。

**一台刚装好的 Windows 电脑，通常是没有 C 编译器的。** 我们这台就没有。

### 2.2 检查一下

打开 **PowerShell**（按 Win 键，输入 powershell，回车），敲：

```powershell
gcc --version
```

- 如果显示一堆版本信息 → 有编译器，跳到第 3 节。
- 如果显示 **"无法将 gcc 项识别为..."** → 没有，继续往下看。

### 2.3 没有怎么办：装一个"便携版"

**不要**去下载 Visual Studio（好几个 G，还要装很久）。
用这一条命令，从国内镜像装一个自带的 C 工具链：

```powershell
pip install ziglang -i https://pypi.tuna.tsinghua.edu.cn/simple
```

**这条命令在做什么**：
- `pip` 是 Python 的包管理器（你这台电脑有 Python）
- `ziglang` 是一个叫 Zig 的工具，它**自带一个完整的 C 编译器**（基于 clang）
- `-i https://...` 是指定从清华的镜像下载（国内快，而且这个网络环境下 GitHub 下载不通）
- 大约下载 98 MB，装完就有编译器了，**不需要管理员权限，不改系统设置**

验证一下：

```powershell
py -3 -m ziglang cc --version
```

看到 `clang version 21.x.x` 就成功了。

> **为什么要记这一条**：如果换了台电脑重新做，这是第一步。
> 这也说明一件事 —— **环境受限时，先分清"必须解决的"和"可以绕开的"**。
> 我要的是"能编译"，不是"必须装 gcc"。

---

## 第 3 节 · 一步一步跑起来（照着敲就行）

**先打开 PowerShell，进到项目目录：**

```powershell
cd "C:\Users\dimidimi\Desktop\E-control project\RoboControl"
```

> 注意：路径里有空格，**必须加双引号**。这是新手最常踩的坑之一。

---

### 步骤 1：编译（把代码变成程序）

```powershell
powershell -ExecutionPolicy Bypass -File build\build_sim.ps1 -Target all
```

**你会看到**（一路 `[build] cc ...` 然后 OK）：

```
[build] compiler : C:\Windows\py.exe -3 -m ziglang cc
[build] cc  Control\pid.c
[build] cc  Simulation\virtual_motor.c
...
[build] link tests.exe
[build] OK -> ...\build\tests.exe
[build] link sim_demo.exe
[build] OK -> ...\build\sim_demo.exe
```

**每个词的意思**：
- `build_sim.ps1` 是构建脚本（一个自动化的"编译流程说明书"）
- `-Target all` 意思是"把测试程序和演示程序都编译出来"
- `-ExecutionPolicy Bypass` 是让 PowerShell 允许运行这个脚本（本地脚本默认被拦）
- `[build] cc xxx.c` 表示正在编译某个源文件

**如果报错**：看第 8 节的"出错对照表"。

---

### 步骤 2：跑测试（确认程序真的对）

```powershell
.\build\tests.exe
```

**你会看到**（很长，最后是这样）：

```
  -- suite [Config] done: 38 checks, 0 failures
  -- suite [PID] done: 71 checks, 0 failures
  -- suite [VirtualMotor] done: 92 checks, 0 failures
  -- suite [MotorManager] done: 175 checks, 0 failures
  -- suite [CommandService] done: 252 checks, 0 failures
  -- suite [MonitorService] done: 271 checks, 0 failures
  -- suite [ClosedLoop] done: 322 checks, 0 failures
-----------------------------------------------------
TOTAL: 322 checks, 0 failures
RESULT: PASS
```

**怎么读这个结果**：
- 每一行 `[xxx] done: N checks, 0 failures` 是一个"测试套件"的结果
- `checks` 是"检查了多少条"，`failures` 是"几条没过"
- **只要 `0 failures` 和 `RESULT: PASS`，就是全对**
- 想更严格（连一个警告都不许有），加 `-Werror` 重新编译：

```powershell
powershell -ExecutionPolicy Bypass -File build\build_sim.ps1 -Target tests -Werror
```

**测试到底在测什么**（举三个例子你就懂了）：

| 测什么 | 怎么测 | 为什么重要 |
|---|---|---|
| PID 算得对吗 | 给一个已知的输入，算出来的数应该等于手算的结果 | 算法错了，后面全白搭 |
| 非法状态转换会被拒绝吗 | 试着重"运行中"直接跳到"初始化"，必须被拒绝 | 防止程序跑进奇怪状态 |
| 反馈断了会怎样 | 把电机任务挂起，看监控任务能不能发现 | 这是"保护功能"能不能救命的检验 |

---

### 步骤 3：跑演示（看它真的在控制电机）

**先看正常场景**：

```powershell
cd docs\data
..\..\build\sim_demo.exe normal trace_normal.csv
```

**你会看到**（前面是场景表，然后是实时输出）：

```
#############################################################
#  RoboControl V1.0  |  host simulation demo
#  mode = normal , duration = 15000 ms
#############################################################
#  scenario:
#    t=   500 ms  =>  STATUS
#    t=  1000 ms  =>  START
#    t=  1500 ms  =>  SET_SPEED 1000
#    t=  5000 ms  =>  SET_SPEED 1500
#    t=  8000 ms  =>  SET_SPEED -800
#    t= 11000 ms  =>  STOP
...
=== RoboControl V1.0 | mode=SIMULATION | boot OK ===
> START
TGT=0.0 ACT=0.0 OUT=0.00 POS=0.0 STATE=RUNNING FAULT=0x00000000
> SET_SPEED 1000
TGT=1000.0 ACT=1031.3 OUT=18.38 POS=485.9 STATE=RUNNING FAULT=0x00000000
...
```

**怎么读**：
- `> START` 是"模拟有人从串口敲了 START"（前面的 `>` 表示这是输入）
- 下面 `TGT=...` 是程序每 500 毫秒打印的一行状态
- **重点看 `ACT` 那一列怎么从 0 变到 1000 再稳住** —— 这就是"控制闭环在工作"

**再看故障场景**（证明保护功能真的管用）：

```powershell
..\..\build\sim_demo.exe fault trace_fault.csv
```

**你会看到**（关键几行）：

```
TGT=1000.0 ACT=1000.0 OUT=20.00 POS=2990.0 STATE=RUNNING FAULT=0x00000000
[scenario] fault injected: motor feedback line cut
TGT=1000.0 ACT=1000.0 OUT=0.00  POS=2990.0 STATE=ERROR   FAULT=0x00000002
```

**读法**：
- `[scenario] fault injected` = 演示程序故意把"电机反馈线剪断"（模拟故障）
- 下一行 `STATE=ERROR` 且 `OUT=0.00` = **监控任务发现了，并且立刻把输出清零**
- `FAULT=0x00000002` 是故障码，`02` 这个位表示"电机反馈过期"
- **从剪断到发现只用了 100 毫秒**（监控任务是 100 毫秒一个周期）

**回到项目根目录**：

```powershell
cd ..\..
```

---

### 步骤 4：把原始数据算成"能写进报告的指标"

```powershell
python tools\analyze_trace.py docs\data\trace_normal.csv docs\data\trace_fault.csv --svg docs\data\step_response.svg --md docs\data\TEST_DATA.md
```

**这条命令在做什么**：把"每 10 毫秒一行的原始数据"（几千行）
算成几个关键指标，并画成图。

**结果在 `docs\data\TEST_DATA.md`**，内容长这样：

| 阶跃 | 超调 (%) | 上升时间 (ms) | 整定时间 (ms) | 稳态误差 |
|---|---|---|---|---|
| 100 -> 1000 | 16.11 | 90 | 570 | +0.000 |
| 1370 -> -800 | 14.50 | 170 | 660 | -0.000 |

**这四个指标是什么意思**（用开车打比方）：

| 指标 | 大白话 | 理想值 |
|---|---|---|
| **超调** | 我要开到 100，结果冲到了 103 才回落 —— 冲过头了多少 | 越小越好 |
| **上升时间** | 从"开始加速"到"达到目标的 90%"用了多久 | 越小越好 |
| **整定时间** | 从开始到"稳定在目标附近不再晃"用了多久 | 越小越好 |
| **稳态误差** | 最后稳定下来，和目标差多少 | 应该接近 0 |

**看图**：用浏览器打开 `docs\data\step_response.svg`，
上半张是速度（虚线=目标，红线=实际），下半张是控制器输出。

---

## 第 4 节 · 代码怎么读（按这个顺序，不会迷路）

**阅读顺序的原则**：从"不依赖别人"的读到"依赖别人"的。
就像读菜谱先看食材表，再看做法。

### 4.1 第一站：`Common\types.h` —— 认识"名词"

**先看这个文件，因为它定义了整个项目在说什么。**

里面定义了 5 组东西，你只要看懂前 4 个就够：

```c
typedef enum { RC_OK = 0, RC_ERROR, RC_INVALID_PARAM, RC_TIMEOUT, RC_BUSY } RC_Result;
```
**这是"返回值"**。就像函数做完事要回一句话：
- `RC_OK` = 成功了
- `RC_INVALID_PARAM` = 你给的参数不对
- `RC_BUSY` = 我正忙 / 排队满了

```c
typedef enum { MOTOR_INIT = 0, MOTOR_READY, MOTOR_RUNNING, MOTOR_STOPPED, MOTOR_ERROR } MotorState;
```
**这是"电机现在处于什么状态"**。像电梯的状态：初始化中 / 待命 / 运行中 / 已停止 / 故障。

```c
typedef struct {
    float target_speed;   float actual_speed;   float position;
    float control_output; MotorState state;     uint32_t update_tick;
} MotorData;
```
**这是"电机的全部信息"**：目标速度、实际速度、位置、控制器输出、状态、最后更新时间。

```c
typedef struct { CommandType type; float value; uint32_t timestamp; } CommandMessage;
```
**这是"一条命令"**：类型（START/STOP/…）、参数（比如速度值）、时间戳。

**看完这一站你应该能回答**：程序里 `MotorState` 有哪几种？`RC_OK` 是什么意思？

---

### 4.2 第二站：`Common\config.h` —— 认识"旋钮"

**这个文件里全是数字。** 所有可调的东西都集中在这，项目里任何其他地方都不许出现"魔法数字"。

**为什么这么做**：如果 `10` 这个数字散落在 20 个文件里，你想把控制周期从 10ms 改成 5ms，
就得改 20 个地方，漏一个就出 bug。**集中在一个文件，改一处就行。**

重点看这几行（每个都有中文注释）：

```c
#define RC_CONTROL_PERIOD_MS   10U      /* 控制任务每 10 毫秒跑一次 */
#define RC_MOTOR_PERIOD_MS     10U      /* 电机任务每 10 毫秒跑一次 */
#define RC_MONITOR_PERIOD_MS   100U     /* 监控任务每 100 毫秒跑一次 */
#define RC_DEBUG_PERIOD_MS     500U     /* 调试任务每 500 毫秒打印一次 */

#define RC_PID_KP    (0.18f)            /* PID 的比例系数 */
#define RC_PID_KI    (1.00f)            /* PID 的积分系数 */
#define RC_PID_KD    (0.00f)            /* PID 的微分系数 */
```

**注意**：文件里分了三段：`【A】ICD 冻结常量`（任务书规定的，不能改）、
`【B】V1.0 整定/工程常量`（我定的，有依据）、`【C】编译期自检`。
**改之前先看清在哪一段** —— 改 A 段是要走审批流程的。

---

### 4.3 第三站：`Control\pid.c` —— 认识"控制器"

**这是整个项目最核心的 100 行，也是最值得读懂的一段。**

**PID 是什么**（用开车保持 60km/h 打比方）：

你现在开 40，想开 60：
- **P（比例）**：现在差 20，那就多踩一点 → 差得越多踩得越深
- **I（积分）**：如果一直差 5 就是上不去，那就**慢慢再加一点**，直到差变成 0
- **D（微分）**：眼看快到了，提前松一点，别冲过头

公式就一行：

```
输出 = kp × 误差 + ki × 累积误差 + kd × 误差变化速度
```

**打开 `pid.c`，看 `PID_Update` 这个函数。** 里面有编号 ①~⑦ 的注释，
这七行**必须按这个顺序执行**，顺序换了结果就不一样：

```c
① error = target - actual;                     /* 差多少 */
② pid->integral += error * dt;                 /* 把误差累积起来 */
③ derivative = (error - pid->last_error) / dt; /* 误差变化多快 */
④ output = kp*error + ki*integral + kd*derivative;  /* 合起来 */
⑤ pid->integral = 限幅(pid->integral, ±integral_limit);  /* 积分别攒太多 */
⑥ output = 限幅(output, ±output_limit);                    /* 输出别超范围 */
⑦ pid->last_error = error;                     /* 记住这次的误差，下次要用 */
```

**两个关键概念**：

- **限幅（clamp）**：把值限制在一个范围内。
  比如输出限幅 ±100，那算出 500 也只能给 100。
  **为什么必须限幅**：真实电机只能接受这么多，给多了要么烧掉要么没反应。

- **积分饱和（windup）**：这是本项目的一个"已知问题"。
  想象你一直踩死油门但车被墙挡住了 —— 你的"再踩深点"的念头会一直累积。
  等墙没了，你已经"想踩"很久了，车会猛地窜出去。
  **积分项也会这样**：输出早就到上限了，积分还在涨。
  → 项目里的超调（冲过头 16%）主要就是这个造成的，详见 `docs/tuning-log.md`。

**看完这一站你应该能回答**：kp/ki/kd 分别是什么作用？为什么输出要限幅？

---

### 4.4 第四站：`Simulation\virtual_motor.c` —— 认识"被控对象"

**这就是那个"驾驶模拟器里的车"。** 只有 5 行公式：

```
加速度 = 100 × 输入 - 2 × 当前速度 - 负载
新速度 = 当前速度 + 加速度 × 时间
新位置 = 当前位置 + 新速度 × 时间
```

**逐项解释**：
- `100 × 输入`：输入越大，加速越快（油门）
- `- 2 × 当前速度`：速度越高，阻力越大（空气阻力/摩擦），所以它会自然稳定在某个速度
- `- 负载`：额外挂个重物

**算一下**：输入 20 时，最终稳定速度 = 100×20 / 2 = **1000**。
这和演示里 OUT=20.00 时速度正好是 1000 对上了。

**为什么写成这样**：控制领域里这叫"一阶系统"，是最简单又能表现真实特性的模型。
**诚实地说**：这是简化模型，不包含电流环、磁饱和这些真实电机才有的东西 ——
任务书里也明确说了它是"演示级简化模型"。

---

### 4.5 第五站：`Service\motor_manager.c` —— 认识"数据所有权"

**这个文件解决的问题是：5 个任务都要看/改电机数据，怎么保证不乱？**

**做法非常简单粗暴：数据只放在这一个文件里，而且加了锁（static）。**

```c
static MotorData s_motor;   /* 唯一的一份电机数据 */
```

**`static` 是什么意思**：这个变量只能被这个文件里的代码访问。
别的文件就算写 `extern MotorData s_motor;` 也链接不到 —— **编译器直接不让**。

**别人想读怎么办**：调 `MotorManager_GetSnapshot()`，拿到一份**拷贝**（值拷贝）。
改拷贝不会影响原件。

**为什么这么做**（关键理解）：
如果数据是一个公共的全局变量，5 个任务都能改，
那"某一刻速度是多少"就永远说不清 —— A 任务刚改完，B 任务就改了，C 任务读到的是哪一份？
**所有权明确之后，这个问题从根上消失了。**

**这个文件里第二重要的事是"状态机"**：

```
INIT    → READY, ERROR
READY   → RUNNING, STOPPED, ERROR
RUNNING → STOPPED, ERROR          ← 注意：不能从 RUNNING 直接回 INIT
STOPPED → RUNNING, READY, ERROR
ERROR   → INIT, READY（仅 RESET 后）  ← 注意：不能从 ERROR 直接到 RUNNING
```

**为什么限制**：像电梯不能在"运行中"直接跳到"初始化"。
**非法转换会返回 `RC_INVALID_PARAM` 并被拒绝。** 测试里有专门验证这一条的用例。

---

### 4.6 第六站：`RTOS\task_control.c` —— 认识"任务"

**任务是什么**：你可以想象厨房里有 5 个厨师，每个人只做一件事，同时干活。

**这个文件是"控制任务"，它的循环长这样**（对应 ICD 第 13 节的八步）：

```c
for (;;) {
    vTaskDelayUntil(&last_wake, 10ms);       /* 1. 睡到下一个 10ms 整点 */
    while (取队列(&msg) == RC_OK) {          /* 2. 看看有没有新命令 */
        处理命令(&msg);                      /* 3. 有就执行 */
    }
    snapshot = 取快照();                     /* 4. 看看电机现在怎么样 */
    if (状态 != RUNNING) 输出 = 0;           /* 5. 没在运行就输出 0 */
    else 输出 = PID_Update(目标, 实际, 0.01);/* 6. 跑 PID */
    提交输出(输出);                          /* 7. 把输出交出去 */
    报告心跳();                              /* 8. 告诉监控"我还活着" */
}
```

**看懂这个循环，你就看懂了整个项目的一半。**

**`vTaskDelayUntil` 和 `vTaskDelay` 的区别**（面试常问）：
- `vTaskDelay(10)` = "睡 10 毫秒"（如果中间被打断过，周期会漂）
- `vTaskDelayUntil(&t, 10)` = "睡到 t+10 这个时间点"（**周期精确，不会累积漂移**）
控制环必须用后者。

---

### 4.7 其余文件（不需要逐行看，知道干什么就行）

| 文件 | 一句话 |
|---|---|
| `Service\command_service.c` | 把 `SET_SPEED 1000` 这行字翻译成命令包；用一个字符一个字符读的方式，不用 `strtok`（因为那个在多任务下不安全） |
| `Service\monitor_service.c` | 体检：心跳超时了吗？反馈还新鲜吗？数值是不是 NaN？速度是不是超出范围了？ |
| `Application\app.c` | 按任务书规定的顺序把系统启动起来，并把 5 个任务创建出来 |
| `Core\main.c` | 电脑上运行的入口：调 启动 → 调 创建任务 → 开始调度 |
| `RTOS\task_motor.c` | 拿到输出 → 喂给虚拟电机 → 把新速度写回去 |
| `RTOS\task_command.c` | 一直等串口数据，攒够一整行就解析并丢进队列 |
| `RTOS\task_monitor.c` | 每 100 毫秒体检一次，发现严重问题就"急停" |
| `RTOS\task_debug.c` | 每 500 毫秒打印一行 `TGT=...` |
| `Common\rc_port.h` | 只声明"我需要读时钟、需要进临界区"，具体谁实现由平台决定（电脑 or 单片机） |
| `RTOS\port_sim\port_sim.c` | 电脑上的"假 FreeRTOS"（用 Windows 纤程实现任务切换） |
| `BSP\bsp_uart_sim.c` | 电脑上的"假串口"（从内存里取命令，输出打到控制台） |

---

## 第 5 节 · 名词词典（碰到不懂的就来这查）

| 名词 | 大白话解释 | 在本项目的哪里 |
|---|---|---|
| **嵌入式** | 写进单片机（一块小芯片）里、直接控制硬件的程序 | 整个项目 |
| **STM32F4** | 一种常用的单片机芯片型号，本项目目标平台 | ICD 里规定的，代码已预留 |
| **FreeRTOS** | 一个"多任务操作系统"，让单片机可以同时跑好几件事 | `RTOS\` 整个目录 |
| **任务 (Task)** | 一个"独立的小程序"，好像厨房里一个厨师 | `task_*.c` 共 5 个 |
| **调度器** | 决定"现在该哪个任务跑"的那个总管 | `RTOS\port_sim\port_sim.c` |
| **优先级** | 谁更"急"。数字越大越急，急的可以打断不急的 | `config.h` 里的 `TASK_PRIO_*` |
| **周期** | 多久跑一次。控制任务是 10 毫秒一次 | `config.h` 里的 `*_PERIOD_MS` |
| **队列 (Queue)** | 一条"传送带"，把数据从一个任务传给另一个，附带自动排队 | `CommandQueue` |
| **队列满** | 传送带上放满了，新的放不下。这时**不能死等**，要立刻返回"忙" | `RC_BUSY` |
| **PID** | 一种自动调节算法，像开车保持定速 | `Control\pid.c` |
| **误差** | 目标减实际。比如想要 1000 现在是 300，误差就是 700 | `error` |
| **超调** | 冲过头了。想要 1000 冲到 1031 才回来 | 16.11% 那个指标 |
| **积分饱和** | 输出早就到上限了，积分还在涨，导致后来冲过头 | 本项目的已知问题（CR-001） |
| **限幅 / clamp** | 把数值限制在一个范围内，超了就砍到边界 | `PID_Update` 第 ⑤⑥ 步 |
| **临界区** | 一小段"谁也别打断我"的代码。像厕所门上的锁，进去锁上，出来开 | `MotorManager_GetSnapshot` |
| **快照 (Snapshot)** | 把当前所有数据**拷贝一份**出来看，而不是直接看原件 | `MotorManager_GetSnapshot()` |
| **中断 (ISR)** | 硬件突然插队。像门铃响了，先放下手上的活去开门，但要马上回来 | `bsp_uart_stm32.c`（硬件侧） |
| **数据所有权** | 规定"这份数据归谁管"，别人只能通过它的接口访问 | `MotorManager` 管 `MotorData` |
| **状态机** | 用"当前处于哪个状态 + 允许怎么切换"来描述逻辑，像电梯 | `MotorManager_SetState` |
| **依赖倒置** | 上层只声明"我需要什么能力"，下层负责实现。这样上层不依赖具体平台 | `Common\rc_port.h` |
| **屏障/分层** | 把代码分成几层，规定"上层可以调下层，下层不能调上层" | `ARCHITECTURE.md` 第 1 节 |
| **单元测试** | 单独测一个函数对不对，不牵扯其他部分 | `Tests\test_pid.c` |
| **集成测试** | 把所有真实任务跑起来测完整流程 | `Tests\test_closed_loop.c` |
| **故障注入** | 故意制造故障，看程序能不能发现。本项目是故意挂起电机任务 | `test_closed_loop.c` |
| **遥测 (Telemetry)** | 把运行数据导出来，供事后分析 | `docs\data\trace_*.csv` |
| **CSV** | 一种纯文本表格格式，每行一条记录，逗号分隔。Excel 能直接打开 | `trace_normal.csv` |
| **ICD** | Interface Control Document，接口控制文档。**就是老师给的任务书** | 那个 `.docx` 文件 |
| **冻结接口** | 任务书里写明"不许改"的那部分（函数名、参数、结构体字段） | `ICD_CHANGE_REQUEST.md` 里反复提到 |
| **子任务书 §27** | ICD 第 27 节，"第一版最小可运行验证"的十条要求 | `ACCEPTANCE.md` 逐条对照 |
| **纤程 (Fiber)** | 比线程更轻的"可暂停执行流"，用来在电脑上模拟任务切换 | `RTOS\port_sim\port_sim.c` |

---

## 第 6 节 · 每一份文档是干什么的（完整版见 `docs\README.md`）

| 文档 | 什么时候看它 | 它回答什么问题 |
|---|---|---|
| `README.md` | **第一份，任何时候** | 这是什么？怎么跑？做成什么样了？有什么做不到的？ |
| `docs\BEGINNER_GUIDE.md` | 就是你现在读的这份 | 从零开始怎么上手 |
| `docs\README.md` | 不确定该看哪份时 | 每份文档是给谁看的、什么时候翻 |
| `docs\BUILD_NARRATIVE.md` | **要讲给别人听之前** | 我是怎么做出来的？踩过哪些坑？哪些决定是我下的？ |
| `docs\ARCHITECTURE.md` | 要读懂/改代码之前 | 模块边界在哪？数据怎么流？并发为什么安全？ |
| `docs\ACCEPTANCE.md` | 被问"你凭什么说做完了" | 十条验收逐条对照 + 可复现的证据 |
| `docs\ICD_CHANGE_REQUEST.md` | 被问"你改没改任务书要求" | 7 项变更，结论是冻结接口一处没改 |
| `docs\tuning-log.md` | 要改 `config.h` 里的参数之前 | kp 凭什么取 0.18？改了怎么验证没变坏？ |
| `docs\PRESENTATION.md` | 上台前一天 | 开场怎么说？会被问什么？怎么答？ |
| `docs\BUILD_STM32.md` | 拿到真实硬件之后 | 怎么上硬件？第一次最容易在哪栽跟头？ |
| `docs\data\TEST_DATA.md` | 要看实测数字 | 超调/上升时间/整定时间是多少（**自动生成，别手改**） |

---

## 第 7 节 · 如果有人问你，你怎么答

### 7.1 最简版（30 秒，照念就行）

> "这是 RoboControl V1.0。它的目标不是让电机转起来，
> 而是**在还没有真电机的时候，就把控制闭环和软件架构完整做出来并验证掉**。
> 整套系统是给 STM32 单片机写的，由 5 个任务组成：
> 10 毫秒的控制任务跑 PID，10 毫秒的电机任务更新虚拟电机模型，
> 命令任务收串口，监控任务做保护，调试任务做观测。
> 默认是仿真模式，在电脑上就能编译、运行、出曲线。
> 我做了 322 项自动化检查全部通过，并且没有改动任务书里任何一个冻结接口。"

### 7.2 提到"虚拟电机"的时候要主动补一句

> "这个虚拟电机是一阶简化模型，不是完整的电机电磁模型 ——
> 它不包含电流环、磁饱和这些东西。
> 任务书里也明确说了它是**演示级简化模型**。
> **知道自己模型不覆盖什么，比假装它覆盖一切更专业。**"

### 7.3 被问"超调 16% 是不是没调好"

> "不是。我按二阶闭环设计取阻尼比 1.0，理论上应该是零超调，
> 但实测是 10.7% 到 16.1%。
> 我先排除了参数算错（上升时间和设计值吻合），
> 再排除了实现错（单元测试逐行验证过算法），
> 最后用离线工具做受控对比，证明**问题在任务书第 7 节冻结的算法本身缺少条件积分抗饱和**。
> 我没有偷偷改它 —— 因为那是标注了'冻结'的接口 ——
> 而是把它量化记录下来，作为下一版的改进项。"

### 7.4 被问"你怎么保证多任务不打架"

> "三条：第一，PID 和虚拟电机各自只被一个任务独占，根本不需要锁；
> 第二，单个数字在 ARM 芯片上是原子读写，也不需要锁；
> 第三，只有'6 个字段的整体拷贝'不是原子的，那一处用了短临界区，
> 里面只有赋值，没有任何可能阻塞的调用。
> **全项目没有用 volatile 当同步手段**，因为 volatile 不提供原子性。"

### 7.5 被问"这东西能跑在真电机上吗"

> "控制逻辑层完全不用改。这是我在做架构时刻意保证的 ——
> 我把电脑和单片机的差异压缩成了 3 个函数（读时钟、进临界区、收发命令队列），
> 由不同平台各自实现。
> 所以换到硬件时只需要替换平台层的文件，那 5 个任务和所有服务层代码一行都不动。
> 硬件部分的文件我已经按接口写全了，但**本机没有 ARM 工具链，没有实际编译验证过** ——
> 这一点我在文档里如实标注了，并写了一份 7 步验收清单。"

---

## 第 8 节 · 出错了怎么办（对照表）

| 你看到的现象 | 大概是什么问题 | 怎么办 |
|---|---|---|
| `无法将 gcc 项识别为...` | 没有编译器 | 回第 2.3 节装 ziglang |
| `无法加载文件 build\build_sim.ps1，因为在此系统上禁止运行脚本` | PowerShell 默认禁止运行本地脚本 | 命令里加 `-ExecutionPolicy Bypass` |
| `No C compiler found` | 脚本没找到编译器 | 先跑 `py -3 -m ziglang cc --version` 确认能出结果 |
| 命令行提示找不到路径 | 路径里有空格没加引号 | 整个路径用双引号包起来 |
| `cannot open docs\data\trace_normal.csv` | 工作目录不对 | 先 `cd docs\data` 再跑演示程序 |
| 演示跑完了但没生成 CSV | 同上，或者路径带空格没引号 | 用"先 `cd` 到目录，再传相对文件名"的方式 |
| 测试里有 `FAIL` 某一行 | 某个检查没过 | 看 `FAIL` 那行后面的 `got X, want Y`，对比一下差多少 |
| 编译报 `error: ...` 一堆 | 代码语法问题 | 看**第一条** error（后面的往往是连锁反应） |
| 程序卡住不动 | 某个任务没有阻塞点 | 等 20 秒，仿真器看门狗会打印 `FATAL: scheduler spin detected` 告诉你卡在哪个任务 |
| `%f` 打印出来是空白 | （仅单片机）newlib-nano 默认不链接浮点格式化 | 链接选项加 `-u _printf_float`，见 `docs\BUILD_STM32.md` 6.1 |

---

## 第 9 节 · 三句话记住整个项目

1. **它是什么**：一套"没有真电机也能完整验证电机控制程序"的软件，用虚拟电机当被控对象。
2. **它怎么运转**：5 个任务各司其职，用队列传递命令，用快照共享数据，
   控制任务每 10 毫秒跑一次 PID 把速度拉向目标。
3. **它凭什么可信**：322 项自动化检查全通过 + 可复现的实测指标 +
   没有改动任务书里任何一个冻结接口 + 明确写清了它做不到什么。

---

## 附：把这份文档变成你自己的话

读完这份指南后，**试着不看文档，用下面三个问题自测**：

1. 如果有人问你"这个项目在干什么"，你能不能说出一句不含专业名词的话？
   （提示：驾驶模拟器）
2. 如果能看一张纸上的代码，你能指出"这行是比例项、这行是积分项"吗？
   （提示：打开 `Control\pid.c`，找带 ①②③ 注释的那七行）
3. 如果有人问"你怎么知道它是对的"，你能说出三个证据吗？
   （提示：322 项检查、实测指标、演示记录）

**三个都能答上来，你就可以讲这个项目了。**
