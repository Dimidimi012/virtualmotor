# ICD 变更请求记录（ICD V1.0 §26 流程）

> ICD §26 规定：发现 ICD 未覆盖或存在冲突的地方，必须
> **提出问题 -> 说明接口/原因/影响模块/替代方案 -> 负责人确认 -> 更新 ICD -> 同步其他方**。
> 本文就是这条流程的落地记录。
>
> 纪律：**本工程没有悄悄改动任何 §3–§11 的冻结结构体或函数签名。**
> 下面每一项要么是"新增"（不破坏既有接口），要么是"发现冲突并显式记录"，
> 要么是"V1.0 保持冻结、列入 V1.1"。

---

## CR-001 — PID 缺少条件积分抗饱和

| 项 | 内容 |
|---|---|
| **接口** | 不改。`PID_Update` 的签名与行为完全遵循 ICD §7 |
| **问题** | ICD §7 冻结的算法只有"积分限幅"，没有"饱和时冻结积分器"。输出被限幅后积分仍按 `error*dt` 继续累积，目标反向时需要额外时间"爬回来"，表现为超调 |
| **量化证据** | 本次实测 4 组阶跃的超调为 **10.7% ~ 16.1%**；单元测试 `已知缺陷留证` 进一步证明：误差已经归零时控制器仍输出满幅 100，且反向误差 -1 持续 1 秒只能把积分从 100 拉回 99 —— 即还需要约 100 秒才能清空 |
| **影响模块** | Control/PID（行为）、ControlTask（周期）、整定参数 |
| **替代方案** | 方案 A：V1.1 增加条件积分（输出饱和且误差方向会加深饱和时冻结积分器）。工具箱 `pid_step_simulate` 的对比显示同一组增益下超调可从 **29.9% 降到 2.0%**。方案 B：保持算法不变，把 `integral_limit` 调得更小，代价是消静差变慢 |
| **V1.0 决定** | **保持冻结**。理由：ICD §7 是明确标注"冻结"的算法规则，在无人确认的情况下静默改动，会破坏"接口宪法"的权威性 —— 而这个权威性正是多 Agent 协作能成立的前提。改为把缺陷、证据与改进路径一起交出 |

---

## CR-002 — `integral_limit` 的量纲与取值规则未定义

| 项 | 内容 |
|---|---|
| **接口** | 不改。`RC_DEFAULT_INTEGRAL_LIMIT` 保持 ICD §20 的冻结值 1000.0f |
| **问题** | ICD §20 只给了数值 1000，没说它与 `ki` 的量纲关系。积分项的实际威力是 `ki * integral`：当 `ki = 1.0` 时它相当于允许积分项单独输出 1000，而 `output_limit` 只有 100 —— 积分项可以轻易压过比例与微分，饱和后极难回收 |
| **影响模块** | Common/config.h、PID 整定 |
| **替代方案** | 在 config.h 中显式定义取值规则：`RC_PID_INTEGRAL_LIMIT = RC_DEFAULT_OUTPUT_LIMIT / RC_PID_KI`，并写进注释与测试 |
| **V1.0 决定** | **新增派生宏，保留原冻结宏**。规则的含义是"让积分项单独作用时最多达到满输出"，量纲清晰、可被测试断言（`Tests/test_config.c` 有对应用例）。ICD §20 的 1000.0f 仍原样保留在文件里，作为接口兼容的默认值 |

---

## CR-003 — RESET 缺少跨任务复位通道，且 §10 未定义 RESET 的转换路径

| 项 | 内容 |
|---|---|
| **接口** | **新增**（不改动已有签名）：`RC_Result MotorManager_RequestReset(void)`、`bool MotorManager_TakeResetRequest(void)` |
| **问题** | ICD §27 要求"RESET -> PID_Reset + VirtualMotor 重置 + READY"，但：<br>1) ICD §9 的冻结接口里**没有任何跨任务复位通道**；<br>2) ICD §18 规则 2 规定 VirtualMotor **只能由 MotorTask 写入**，而处理 CMD_RESET 的是 ControlTask；<br>3) ICD §10 的转换表没有说明 RESET 应走哪条路径（ERROR -> READY 只标注了"仅 RESET 后"，没说 RESET 从 RUNNING / STOPPED 怎么走） |
| **影响模块** | Service/MotorManager、RTOS/task_control.c、RTOS/task_motor.c |
| **替代方案** | 方案 A：让 MotorManager 直接持有 VirtualMotor —— 违反 §18 规则 2 与 §14 的任务职责划分。方案 B：用 `MOTOR_INIT` 当复位信号 —— 存在竞态，MotorTask 可能错过。方案 C（采用）：在 MotorManager 里加一对最小协调接口 |
| **V1.0 决定** | 采用方案 C，并**显式定义 RESET 的合法路径**：<br>RUNNING/READY -> STOPPED -> READY；STOPPED -> READY；ERROR -> READY；INIT -> READY。<br>全部落在 ICD §10 已允许的转换之内，**没有任何一次跳转违反冻结表**。<br>同时规定：`ERROR -> READY` 对外接口 `MotorManager_SetState` **永久拒绝**，只有 `RequestReset` 能打开 —— 把"仅 RESET 后"从注释变成参数控制的可达性 |

---

## CR-004 — CommandService 需要三个扩展访问器

| 项 | 内容 |
|---|---|
| **接口** | **新增**：`CommandService_Init`、`CommandService_TakePidResetRequest`、`CommandService_TakeStatusRequest`、`CommandService_GetSendRejectCount`、`CommandService_GetParseRejectCount` |
| **问题** | ICD §11 冻结了 Parse/Send/Handle 三个函数，但 Handle 是 `void` 返回。而 ICD §6 规定 PIDController 不暴露给 Command 模块修改（所以 RESET 时 PID_Reset 只能由 ControlTask 执行），ICD §13 又禁止 ControlTask 执行 printf（所以 STATUS 的打印只能由 DebugTask 执行）。Handle 必须能"请求"却不能"执行" |
| **影响模块** | Service/CommandService、RTOS/task_control.c、RTOS/task_debug.c |
| **替代方案** | 方案 A：改 Handle 返回结构体 —— 破坏 §11 冻结签名。方案 B：让 CommandService 直接调用 PID_Reset 与 printf —— 违反 §6 与 §13。方案 C（采用）：Handle 置位、由有权者取走 |
| **V1.0 决定** | 采用方案 C。另外补上两个计数器：`RC_FAULT_QUEUE_FULL` 与 `RC_FAULT_CMD_PARSE` 这两个故障位如果没有计数来源就是死代码，而现场最有价值的恰恰是"哪一类故障、多少次" |

---

## CR-005 — 新增 `Common/rc_port.h` 平台移植接口

| 项 | 内容 |
|---|---|
| **接口** | **新增文件** `Common/rc_port.h`，声明 `RC_Port_GetTick` / `RC_Port_EnterCritical` / `RC_Port_ExitCritical` / `RC_Port_CmdQueueCreate` / `RC_Port_CmdQueueSend` / `RC_Port_CmdQueueReceive` |
| **问题** | 三处硬冲突：<br>1) ICD §4 要求 MotorManager 记录 `update_tick`，但 §23 禁止它 include FreeRTOS.h；<br>2) ICD §18 规则 5 允许 MotorManager 用短临界区，同上；<br>3) ICD §11 要求 `CommandService_Send` 直接发送到 CommandQueue，而 §23 只允许它依赖 Common + MotorManager |
| **影响模块** | Common、Service/*、RTOS/port_sim、RTOS/port_freertos |
| **替代方案** | 方案 A：允许 Service 层直接 include FreeRTOS.h —— 破坏了"Service 可在 PC 上脱离 RTOS 测试"的能力。方案 B：给每个函数加回调参数 —— 污染 §9/§11 冻结签名。方案 C（采用）：依赖倒置 |
| **V1.0 决定** | 采用方案 C。Common 层只**声明**平台需要提供的原语（纯头文件、不 include 任何平台头文件），由 `port_sim` 或 `port_freertos` 分别实现。<br>收益是直接可验证的：RTOS 任务代码在主机和 STM32 上**一字不改** |

---

## CR-006 — MotorManager 的输出安全不变式与故障严重性分类

| 项 | 内容 |
|---|---|
| **接口** | 不改签名。`MotorManager_SetControlOutput` 增加一条行为约定 |
| **问题** | ICD §13 第 5 条要求 ControlTask 在非 RUNNING 时输出 0，ICD §14 第 5 条要求 MotorTask 在 STOPPED/ERROR 时输入置 0。安全依赖散落在两个任务里，任何一处被改错都没有第二道防线。<br>另：ICD §16 说"严重错误 -> StopOutput -> ERROR"，但没说**哪些**故障算严重 —— 若把所有故障都当严重，串口打错一个字就会让电机急停 |
| **影响模块** | Service/MotorManager、RTOS/task_monitor.c、Common/config.h |
| **替代方案** | 方案 A：只保留任务层的检查 —— 单点失效。方案 B：把所有故障都当严重 —— 可用性灾难。方案 C（采用）：在 MotorManager 内部强制不变式 + 显式定义 `RC_FAULT_SEVERE_MASK` |
| **V1.0 决定** | 采用方案 C。<br>不变式：`state != MOTOR_RUNNING` 时输出一律存为 0，且**非有限值（NaN/Inf）直接置 0 并返回 RC_INVALID_PARAM**。这样"非运行态执行器一定得不到非零输出"与具体任务实现无关地成立。<br>分类：只有"**我们已经不知道电机正在发生什么**"才算严重（心跳丢失、反馈过期、NaN/Inf、超范围、长期无响应）；命令层面的故障（队列满、解析失败）只记录。测试对两个方向都有断言 |

---

## CR-007 — 新增目录与文件（不涉及冻结接口）

| 项 | 内容 |
|---|---|
| **接口** | 不改动任何 §3–§11 冻结内容 |
| **问题** | ICD §24 的"推荐真实文件骨架"不足以承载 §28 要求的单元测试、演示与数据采集，也缺少 §2 提到的 Application 层的落地位置 |
| **影响模块** | 工程结构 |
| **替代方案** | 方案 A：把测试塞进现有文件 —— 污染产品代码。方案 B：另建一个仓库 —— 增加协作成本 |
| **V1.0 决定** | 新增 `Application/`（§2 已提到但 §24 未给目录）、`Tests/`、`Sim/`、`tools/`、`docs/`、`build/`，以及在 Common 下新增 `rc_port.h`。<br>所有 ICD §24 列出的文件**均已按原名原位创建**，新增内容全部是叠加，不替换 |

---

## 变更总览

| 编号 | 类型 | 是否改动冻结接口 | V1.0 处理 |
|---|---|---|---|
| CR-001 | 缺陷记录 | 否 | 保持冻结，量化留证，建议 V1.1 |
| CR-002 | 澄清 + 派生宏 | 否 | 新增派生宏，保留原冻结值 |
| CR-003 | 新增 2 个函数 | 否 | 已实现，并显式定义 RESET 路径 |
| CR-004 | 新增 5 个函数 | 否 | 已实现 |
| CR-005 | 新增 1 个头文件 | 否 | 已实现 |
| CR-006 | 行为约定 + 新增 1 个宏 | 否 | 已实现 |
| CR-007 | 新增目录 | 否 | 已实现 |

**结论：ICD V1.0 §3–§11 的冻结结构体与函数签名，无一处被修改。**
