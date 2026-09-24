# ICD §27 验收对照表

> ICD §27 规定了"第一版最小可运行验证"的十条。本文逐条给出**状态、证据、以及否证方式**。
> "证据"一律指向可复现的命令或代码位置，不接受"我测过了"。

复现命令：

```powershell
cd RoboControl
powershell -ExecutionPolicy Bypass -File build\build_sim.ps1 -Target tests
.\build\tests.exe
```

---

## 1. 十条验收逐条对照

| # | ICD §27 要求 | 状态 | 证据 |
|---|---|---|---|
| 1 | 启动系统后 state = READY | ✅ | 测试 `ICD §27-1`；实现 `Application/app.c` 第 7 步 `MotorManager_SetState(MOTOR_READY)` |
| 2 | UART 输入 START -> state = RUNNING | ✅ | 测试 `ICD §27-2/3/4/5/6`；演示 transcript `docs/data/demo_normal_console.txt` |
| 3 | UART 输入 SET_SPEED 1000 -> target_speed = 1000 | ✅ | 同上；命令经 `CommandService_Parse -> Send -> CommandQueue -> ControlTask` 全链路 |
| 4 | ControlTask 每 10 ms 运行 PID | ✅ | `RTOS/task_control.c` 用 `vTaskDelayUntil` 固定周期；仿真按 1 ms tick 逐拍可数 |
| 5 | MotorTask 每 10 ms 更新 VirtualMotor | ✅ | `RTOS/task_motor.c`；`update_tick` 由 MotorManager 自动打戳 |
| 6 | actual_speed 逐渐逼近 target_speed | ✅ | 阶跃 100->1000：上升 90 ms，整定 570 ms，**稳态误差 +0.000** |
| 7 | Debug 每 500 ms 输出曲线关键数据 | ✅ | 测试断言 6 秒内输出 10~14 行（即 500 ms 周期，既不丢也不刷屏） |
| 8 | 输入 STOP -> output = 0，state = STOPPED | ✅ | 测试 `ICD §27-8`；输出清零由 MotorManager 的安全不变式强制 |
| 9 | RESET -> PID_Reset + VirtualMotor 重置 + READY | ✅ | 测试 `ICD §27-9`：目标归零、输出归零、**速度与位置都回到 0** |
| 10 | 故障注入：停止 Motor 更新 -> Monitor 最终 ERROR | ✅ | 测试 `ICD §27-10` + 演示 `sim_demo.exe fault`；实测 **100 ms 内**检出 |

**合计 10/10 通过。**

---

## 2. 超出 §27 的补充验证

这些不是 ICD 要求的，但它们才是"这套架构到底站不站得住"的真正检验：

| 场景 | 断言 | 为什么值得测 |
|---|---|---|
| RESET 不能掩盖仍然存在的故障 | 故障源不消失时系统会**重新**进入 ERROR | 防止现场养成"按复位接着跑"的危险习惯 |
| 故障源恢复后 RESET | 回到 READY 且无残留故障 | RESET 必须是唯一的合法出路，且真的有效 |
| 非法命令（解析失败） | 记录故障位，但**系统继续 RUNNING** | 可用性问题不能被升级成安全问题 |
| 非法状态转换 | `ERROR` 状态下 `START` 被拒绝 | ICD §10 的禁止转换在真实任务里确实生效 |
| STOP -> START 往返 | 全程零故障 | 反复操作不应留下状态残留 |
| 命令队列溢出 | 返回 `RC_BUSY` 并计数，不阻塞 | ICD §15 的"不得无限阻塞" |

---

## 3. ICD §28 阶段完成情况

| 阶段 | ICD §28 内容 | 状态 | 交付物 |
|---|---|---|---|
| A | 普通 C：PID 单元测试 + VirtualMotor 单元测试 | ✅ | `Tests/test_pid.c`、`test_virtual_motor.c` |
| B | 无 UART 的 FreeRTOS：ControlTask + MotorTask + DebugTask | ✅ | `RTOS/task_control.c`、`task_motor.c`、`task_debug.c` + `PortableTests` 集成套件 |
| C | 命令系统：CommandQueue + START/STOP/SET_SPEED | ✅ | `Service/command_service.c`、`RTOS/task_command.c` |
| D | 保护：Monitor + Timeout + ERROR | ✅ | `Service/monitor_service.c`、`RTOS/task_monitor.c` + 故障注入测试 |
| E | 展示：README + 架构图 + 测试数据 + 项目讲解 | ✅ | `README.md`、`docs/ARCHITECTURE.md`、`docs/data/`、`docs/PRESENTATION.md` |

ICD §28 特别强调"**先验证控制闭环，再加通信**"。实际执行顺序也是照此：
先让 PID + VirtualMotor 在纯 C 下跑通（阶段 A），
再接上任务与队列（阶段 B/C），最后才加保护与展示（阶段 D/E）。

---

## 4. 质量门禁

| 项 | 标准 | 现状 |
|---|---|---|
| 编译器警告 | 零警告 | ✅ 在 `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wundef -Wmissing-prototypes -Wstrict-prototypes` 下零警告 |
| 自动化测试 | 全绿 | ✅ 322 checks, 0 failures |
| 测试退出码 | 0 = 通过 | ✅ 可直接用作 CI 门禁 |
| 可复现性 | 仿真逐拍确定 | ✅ 确定性纤程调度，同输入必得同输出 |

打开 `-Werror` 后仍然构建通过：

```powershell
powershell -ExecutionPolicy Bypass -File build\build_sim.ps1 -Target tests -Werror
```
