/**
 * @file    test_main.c
 * @brief   单元测试入口 —— 对应 ICD V1.0 §28 阶段 A/B/C/D
 *
 * 运行： build/tests.exe
 * 退出码：0 表示全部通过，非 0 表示存在失败用例（可直接用于 CI 门禁）。
 */

#include <stdio.h>
#include "test_framework.h"

int main(void)
{
    /* 关掉 stdout 缓冲：测试一旦卡死，缓冲会把"卡在哪一个用例"这条
     * 唯一的线索吞掉。测试程序的可观测性优先于 I/O 性能。 */
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("=====================================================\n");
    printf(" RoboControl V1.0 - unit test run\n");
    printf(" ICD V1.0 §28 stages A/B/C/D\n");
    printf("=====================================================\n");

    TestSuite_Config();
    TestSuite_PID();
    TestSuite_VirtualMotor();
    TestSuite_MotorManager();
    TestSuite_CommandService();
    TestSuite_MonitorService();

    printf("\n>>> entering ClosedLoop integration suite (runs the real scheduler)\n");
    TestSuite_ClosedLoop();

    printf("-----------------------------------------------------\n");
    printf("TOTAL: %d checks, %d failures\n", g_tc_checks, g_tc_failures);
    printf("RESULT: %s\n", (g_tc_failures == 0) ? "PASS" : "FAIL");

    return (g_tc_failures == 0) ? 0 : 1;
}
