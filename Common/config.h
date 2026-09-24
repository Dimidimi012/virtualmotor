/**
 * @file    config.h
 * @brief   RoboControl V1.0 全工程可调参数集中管理 —— 对应 ICD V1.0 第 20 节
 *
 * ICD §20：所有可调常量集中管理。禁止业务代码大量出现 10、100、0.01 等 Magic Number。
 *
 * 本文件按三段组织：
 *   【A】ICD §20 冻结常量      —— 原文照抄，任何人不得擅自改动数值
 *   【B】V1.0 整定/工程常量    —— ICD 未列出但工程可运行所必需，逐条给出量纲与依据
 *   【C】量纲与来源说明        —— 三个月后仍能解释每个数字从哪来
 *
 * 变更纪律（ICD §26）：改动【A】段任何数值必须走 ICD Change Request；
 * 改动【B】段必须同步更新 docs/tuning-log.md。
 */

#ifndef RC_COMMON_CONFIG_H
#define RC_COMMON_CONFIG_H

#include "types.h"

/* =========================================================================
 * 【A】ICD §20 冻结常量（原文照抄，禁止擅改）
 * ====================================================================== */

/* ---- 任务周期（ICD §12 任务表 / §20） ---- */
#define RC_CONTROL_PERIOD_MS      10U     /**< ControlTask 周期，10 ms */
#define RC_MOTOR_PERIOD_MS        10U     /**< MotorTask   周期，10 ms */
#define RC_MONITOR_PERIOD_MS      100U    /**< MonitorTask 周期，100 ms */
#define RC_DEBUG_PERIOD_MS        500U    /**< DebugTask   周期，500 ms */

/* ---- 控制默认参数（ICD §7） ---- */
#define RC_DEFAULT_DT             0.01f   /**< 默认控制周期对应的时间步长 = 10 ms */
#define RC_DEFAULT_OUTPUT_LIMIT   100.0f  /**< PID 输出限幅（与被控量同量纲） */
#define RC_DEFAULT_INTEGRAL_LIMIT 1000.0f /**< PID 积分限幅，单位 error·s
                                               【注意】ICD §20 未说明该值与 ki 的量纲关系，
                                               直接使用会导致积分项独大（ki*1000 >> output_limit）。
                                               应用实际实参见【B】段 RC_PID_INTEGRAL_LIMIT，
                                               问题记录见 docs/ICD_CHANGE_REQUEST.md CR-002。 */

/* ---- 通信与量程 ---- */
#define RC_CMD_QUEUE_LENGTH       8U      /**< CommandQueue 深度（ICD §5 / §20） */
#define RC_MAX_TARGET_SPEED       3000.0f /**< 目标速度合法性上限，单位 mm/s（ICD §20） */

/* =========================================================================
 * 【B】V1.0 整定 / 工程常量
 * ====================================================================== */

/* ---- B1. 版本号（skill 验收要求：版本号写在代码里） ---- */
#define RC_VERSION_MAJOR          1
#define RC_VERSION_MINOR          0
#define RC_VERSION_STRING         "RoboControl V1.0"

/* ---- B2. 运行模式（ICD §0：默认 Simulation） ---- */
#define RC_APP_DEFAULT_RUN_MODE   RC_MODE_SIMULATION

/* ---- B3. 任务优先级（ICD §12 建议用相对宏，而不是散落数字） ----
 * FreeRTOS 数值越大优先级越高；tskIDLE_PRIORITY = 0。
 * ICD §12 表：Control=High, Motor=High, Command=Medium, Monitor=Low, Debug=Low
 * 说明：Control 与 Motor 同为 High，是因为两者被 ICD 冻结为同一优先级（§12），
 *       同优先级的两个 10 ms 任务由 FreeRTOS 时间片轮转，实测可满足闭环。
 */
#define TASK_PRIO_BASE            (1U)
#define TASK_PRIO_DEBUG           (TASK_PRIO_BASE + 0U)   /**< 1  Low    */
#define TASK_PRIO_MONITOR         (TASK_PRIO_BASE + 1U)   /**< 2  Low    */
#define TASK_PRIO_COMMAND         (TASK_PRIO_BASE + 2U)   /**< 3  Medium */
#define TASK_PRIO_MOTOR           (TASK_PRIO_BASE + 3U)   /**< 4  High   */
#define TASK_PRIO_CONTROL         (TASK_PRIO_BASE + 3U)   /**< 4  High   */
#define RC_CONFIG_MAX_PRIORITIES  (6U)                    /**< FreeRTOS configMAX_PRIORITIES */

/* ---- B4. 任务栈深度 ----
 * 单位：FreeRTOS usStackDepth 的 StackType_t 字数（不是字节）。
 * Cortex-M4 上 StackType_t = uint32_t，故 128 words = 512 B。
 * DebugTask 需要更大栈：printf/格式化在栈上开缓冲区（ICD §17 允许其阻塞）。
 */
#define TASK_STACK_CONTROL        (256U)   /**< 256 words = 1 KB */
#define TASK_STACK_MOTOR          (256U)   /**< 256 words = 1 KB */
#define TASK_STACK_COMMAND        (256U)   /**< 256 words = 1 KB */
#define TASK_STACK_MONITOR        (256U)   /**< 256 words = 1 KB */
#define TASK_STACK_DEBUG          (384U)   /**< 384 words = 1.5 KB */

/* ---- B5. PID 整定参数（V1.0 仅初始化阶段设定，ICD §6） ----
 * 对象（VirtualMotor 一阶模型，见 Simulation/virtual_motor.h）：
 *     G(s) = motor_gain / (s + damping) = 100 / (s + 2) = 50 / (0.5 s + 1)
 *     即直流增益 K = 50，时间常数 tau = 0.5 s
 * 设计目标：闭环二阶系统取阻尼比 zeta = 1.0、自然频率 wn = 10 rad/s
 *     特征方程 s^2 + (damping + motor_gain*kp) s + motor_gain*ki = 0
 *     wn^2   = motor_gain * ki = 100          -> ki = 1.00
 *     2*zeta*wn = damping + motor_gain*kp = 20 -> kp = 0.18
 * kd = 0 的理由：对象是一阶无噪声模型，D 项只放大噪声不改善相位裕度
 *     （见 docs/tuning-log.md 2026-xx-xx 条目的对比数据）。
 * 【纪律】改控制周期必须重算本组参数（pid-tuning 规范）。
 */
#define RC_PID_KP                 (0.18f)                       /**< 无量纲 */
#define RC_PID_KI                 (1.00f)                       /**< 输出/(误差·s) */
#define RC_PID_KD                 (0.00f)                       /**< 输出/(误差/s) */
#define RC_PID_OUTPUT_LIMIT       (RC_DEFAULT_OUTPUT_LIMIT)     /**< 与 ICD §20 一致 */
#define RC_PID_INTEGRAL_LIMIT     (RC_DEFAULT_OUTPUT_LIMIT / RC_PID_KI)
                                                                /**< = 100 error·s
                                                                     取值规则：让积分项单独
                                                                     作用时最多达到满输出，
                                                                     即 ki * integral_limit
                                                                     == output_limit。
                                                                     见 CR-002。 */
/* ---- B6. VirtualMotor 对象参数（ICD §8；演示级一阶简化动力学模型） ----
 * acceleration = motor_gain * input - damping * speed - load
 *   motor_gain [速度·s^-1 / 输入单位]  —— 对应 Kt/J
 *   damping    [s^-1]                  —— 对应 B/J，其倒数即机械时间常数 tau
 *   load       [速度·s^-1]             —— 恒定负载扰动，V1.0 默认 0
 * 取 gain=100、damping=2 的依据：
 *   tau = 1/damping = 0.5 s（观测窗口内可见，且远慢于 10 ms 控制周期，符合带宽分离）；
 *   满输出 100 时稳态速度 = gain/damping*100 = 5000 > RC_MAX_TARGET_SPEED 3000，
 *   保证任何合法指令都不会被输出限幅永久顶死。
 */
#define RC_VMOTOR_GAIN            (100.0f)
#define RC_VMOTOR_DAMPING         (2.0f)
#define RC_VMOTOR_LOAD            (0.0f)

/* ---- B7. Monitor 保护阈值（ICD §16 基础保护） ---- */
#define RC_HB_TIMEOUT_CONTROL_MS   (100U)   /**< Control 心跳超时：允许连续丢 10 拍 */
#define RC_HB_TIMEOUT_MOTOR_MS     (100U)   /**< Motor 反馈超时：同上 */
#define RC_NORESPONSE_TIMEOUT_MS   (2000U)  /**< RUNNING 且目标非零却长期无响应 */
#define RC_NORESPONSE_SPEED_EPS    (1.0f)   /**< 判定「无响应」的速度阈值 */
#define RC_NORESPONSE_TARGET_MIN   (10.0f)  /**< 只有目标大于该值才判定无响应 */
#define RC_MONITOR_MAX_ABS_SPEED   (RC_MAX_TARGET_SPEED * 1.5f)
#define RC_MONITOR_MAX_ABS_OUTPUT  (RC_DEFAULT_OUTPUT_LIMIT * 1.1f)

/* ---- B8. 故障码寄存器（skill 可观测性要求；可位或） ----
 * ICD §16 只要求「可解释的基础保护」，因此 V1.0 用单寄存器位图，
 * 不做故障树、不做 Flash 掉电保存（列入 V1.1）。
 */
#define RC_FAULT_NONE              (0x00000000U)
#define RC_FAULT_CONTROL_HB        (0x00000001U)  /**< Control 心跳超时      */
#define RC_FAULT_MOTOR_FB          (0x00000002U)  /**< Motor 反馈过期        */
#define RC_FAULT_NO_RESPONSE       (0x00000004U)  /**< RUNNING 长期无响应    */
#define RC_FAULT_NONFINITE         (0x00000008U)  /**< 速度/输出 NaN 或 Inf  */
#define RC_FAULT_OUT_OF_RANGE      (0x00000010U)  /**< 速度/输出超出量程     */
#define RC_FAULT_QUEUE_FULL        (0x00000020U)  /**< CommandQueue 满       */
#define RC_FAULT_CMD_PARSE         (0x00000040U)  /**< 命令解析失败          */

/* ---- B8b. 触发保护动作的故障掩码（V1.0 设计决定） ----
 * 判据只有一条：**这个故障是否意味着「我们已经不知道电机正在发生什么」**。
 *   心跳/反馈丢失、NaN/Inf、超范围、长期无响应 —— 闭环已失控，必须 StopOutput + ERROR。
 * 反之，命令层面的问题（队列满、解析失败）只记录、不停机：
 *   在串口上打错一个字就急停整台机器，是把「可用性问题」升级成了「安全问题」，
 *   现场结果是没人敢用这条调试通道。
 * 这个区分必须显式写出来，否则「哪些故障会停车」这件事会散落在代码里，谁也说不清。
 */
#define RC_FAULT_SEVERE_MASK       (RC_FAULT_CONTROL_HB | RC_FAULT_MOTOR_FB | \
                                    RC_FAULT_NO_RESPONSE | RC_FAULT_NONFINITE | \
                                    RC_FAULT_OUT_OF_RANGE)

/* ---- B9. CommandQueue 发送超时（ICD §15：Queue 满不得无限阻塞） ---- */
#define RC_CMD_QUEUE_SEND_TIMEOUT_TICKS (0U)  /**< 0 = 不阻塞，直接返回 RC_BUSY */

/* ---- B10. 命令文本缓冲（ICD §15：CommandTask 是唯一字符串命令入口） ---- */
#define RC_CMD_LINE_MAX            (48U)    /**< 单行命令最大长度，含结尾 '\0' */
#define RC_CMD_TOKEN_MAX           (3U)     /**< 最多 3 个 token：NAME / 参数 */
#define RC_CMD_TOKEN_LEN           (16U)    /**< 单个 token 最大长度 */

/* ---- B11. 调试输出（ICD §17：低优先级消费者，禁止阻塞 ControlTask） ---- */
#define RC_DEBUG_UART_BAUD         (115200U)
#define RC_UART_TX_TIMEOUT_MS      (50U)   /**< 阻塞式 UART 发送超时；只允许 DebugTask 使用 */

/* =========================================================================
 * 【C】编译期自检
 * 把「配置错误」提前到编译期暴露，而不是等到运行期才发现。
 * ====================================================================== */

/* 控制周期与 RC_DEFAULT_DT 必须一致，否则 PID 的 dt 与实际周期不符（ICD §7 强调点） */
#if (RC_CONTROL_PERIOD_MS != 10U)
#error "RC_CONTROL_PERIOD_MS 改变后必须同步 RC_DEFAULT_DT，并重新整定 kp/ki/kd（见 config.h B5）"
#endif

#if (RC_CMD_QUEUE_LENGTH < 2U)
#error "RC_CMD_QUEUE_LENGTH 至少为 2，否则 START/STOP 连发会丢命令"
#endif

/* 优先级必须落在 FreeRTOS 合法区间内 */
#if (TASK_PRIO_CONTROL >= RC_CONFIG_MAX_PRIORITIES)
#error "TASK_PRIO_CONTROL 超出 configMAX_PRIORITIES，FreeRTOS 会断言失败"
#endif

#if (TASK_PRIO_DEBUG < 1U)
#error "所有任务优先级必须高于 tskIDLE_PRIORITY(0)"
#endif

/* 浮点不变量说明：
 * C11 的 _Static_assert 要求「整数常量表达式」，浮点比较不是
 * （clang 会以 -Wgnu-folding-constant 警告并作为 GNU 扩展折叠）。
 * 为了既在构建阶段暴露配置错误、又不引入非标准扩展，
 * 浮点不变量统一放在 Tests/test_config.c 中断言，
 * 由 build\build_sim.ps1 在每次构建时自动运行。 */

#endif /* RC_COMMON_CONFIG_H */
