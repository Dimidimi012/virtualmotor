/**
 * @file    test_framework.h
 * @brief   极简单元测试框架（无第三方依赖）
 *
 * ICD §28 阶段 A 要求「普通 C PID 单元测试 + VirtualMotor 单元测试」。
 * 为了不让工程为了测试引入 STM32 之外的重型依赖，这里只用 ~60 行实现
 * 「断言 + 计数 + 报告」三件事，任何 C99 编译器都能跑。
 */

#ifndef RC_TEST_FRAMEWORK_H
#define RC_TEST_FRAMEWORK_H

#include <stdio.h>

extern int         g_tc_checks;
extern int         g_tc_failures;
extern const char *g_tc_case;

/** @brief 开始一个测试用例 */
#define TC_BEGIN(name)                                                     \
    do {                                                                   \
        g_tc_case = (name);                                                \
        printf("  [CASE] %s\n", g_tc_case);                                \
    } while (0)

/** @brief 断言条件为真 */
#define TC_CHECK(cond)                                                     \
    do {                                                                   \
        ++g_tc_checks;                                                     \
        if (!(cond)) {                                                     \
            ++g_tc_failures;                                               \
            printf("    FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                  \
    } while (0)

/** @brief 断言两浮点数在容差内相等 */
#define TC_NEAR(actual, expected, tol)                                     \
    do {                                                                   \
        double tc_a = (double)(actual);                                    \
        double tc_e = (double)(expected);                                  \
        double tc_t = (double)(tol);                                       \
        ++g_tc_checks;                                                     \
        if (!((tc_a - tc_e) <= tc_t && (tc_e - tc_a) <= tc_t)) {           \
            ++g_tc_failures;                                               \
            printf("    FAIL  %s:%d  got %.9g, want %.9g (tol %.3g)\n",    \
                   __FILE__, __LINE__, tc_a, tc_e, tc_t);                  \
        }                                                                  \
    } while (0)

/** @brief 打印本文件结尾处的总结行 */
#define TC_SUITE_REPORT(suite)                                             \
    printf("  -- suite [%s] done: %d checks, %d failures\n",               \
           (suite), g_tc_checks, g_tc_failures)

/* 各测试套件入口 */
void TestSuite_Config(void);
void TestSuite_PID(void);
void TestSuite_VirtualMotor(void);
void TestSuite_MotorManager(void);
void TestSuite_CommandService(void);
void TestSuite_MonitorService(void);
void TestSuite_ClosedLoop(void);   /* ICD §28 阶段 B/C/D 端到端集成 */

#endif /* RC_TEST_FRAMEWORK_H */
