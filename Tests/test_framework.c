/**
 * @file    test_framework.c
 * @brief   极简单元测试框架的全局状态定义
 */

#include "test_framework.h"

int         g_tc_checks   = 0;
int         g_tc_failures = 0;
const char *g_tc_case     = "";
