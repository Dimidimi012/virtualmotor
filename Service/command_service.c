/**
 * @file    command_service.c
 * @brief   命令服务实现 —— 对应 ICD V1.0 第 11 / 15 / 21 节
 *
 * 解析器设计说明：
 *   - 不使用 strtok：它有静态内部状态，不可重入，在多任务环境下是隐患；
 *   - 不使用 malloc：嵌入式禁用动态分配是常见纪律，且队列本身只传值；
 *   - 只在栈上开固定大小缓冲区，最坏情况可静态核算（见 RC_CMD_TOKEN_*）。
 */

#include "command_service.h"
#include "motor_manager.h"
#include "config.h"
#include "rc_port.h"

#include <stdlib.h>   /* strtof */
#include <string.h>

/* -------------------------------------------------------------------------
 * 内部状态
 * ---------------------------------------------------------------------- */
static bool     s_pid_reset_request;    /* ControlTask 取走 */
static bool     s_status_request;       /* DebugTask   取走 */
static uint32_t s_send_reject_count;    /* 队列满被拒次数    */
static uint32_t s_parse_reject_count;   /* 解析失败次数      */

/* Token 化错误哨兵：与「0 个 token」区分开 */
#define CMD_TOKEN_ERR   (0xFFU)

/* 前置声明：Parse 的每个失败分支都走这里，定义放在文件末尾，
 * 保证"记一次解析失败"只有唯一实现，不会漏记某个 return 分支。 */
static RC_Result CommandService_ParseReject(void);

/* =========================================================================
 * 内部：把一行文本切成大写 token
 *
 * 返回 token 个数；遇到超长 token 返回 CMD_TOKEN_ERR。
 * 只接受空格 / 制表符 / 逗号作为分隔符，CR/LF 视为分隔与终止。
 * ====================================================================== */
static uint8_t CommandService_Tokenize(const char *text,
                                       char tokens[RC_CMD_TOKEN_MAX][RC_CMD_TOKEN_LEN])
{
    const char *p = text;
    uint8_t count = 0U;
    uint8_t len;
    char    c;

    while ((*p != '\0') && (count < RC_CMD_TOKEN_MAX)) {
        /* 跳过分隔符 */
        while ((*p == ' ') || (*p == '\t') || (*p == ',') ||
               (*p == '\r') || (*p == '\n')) {
            ++p;
        }
        if (*p == '\0') {
            break;
        }

        len = 0U;
        while ((*p != '\0') && (*p != ' ') && (*p != '\t') && (*p != ',') &&
               (*p != '\r') && (*p != '\n')) {
            if (len >= (uint8_t)(RC_CMD_TOKEN_LEN - 1U)) {
                /* 超长 token：宁可整条判非法，也不要截断后"看似命中"一条错误命令 */
                return CMD_TOKEN_ERR;
            }
            c = *p;
            if ((c >= 'a') && (c <= 'z')) {
                c = (char)(c - 'a' + 'A');
            }
            tokens[count][len] = c;
            ++len;
            ++p;
        }
        tokens[count][len] = '\0';
        ++count;
    }

    return count;
}

/* =========================================================================
 * 内部：严格浮点解析（整串必须都是数字）
 * ====================================================================== */
static bool CommandService_ParseFloat(const char *s, float *out)
{
    char *end = 0;
    float value;

    if ((s == 0) || (out == 0) || (*s == '\0')) {
        return false;
    }

    value = strtof(s, &end);

    if (end == s) {
        return false;                  /* 一个数字都没消费掉 */
    }
    while ((*end == ' ') || (*end == '\t') || (*end == '\r') || (*end == '\n')) {
        ++end;
    }
    if (*end != '\0') {
        return false;                  /* 尾部有垃圾，例如 "1000abc" */
    }
    if (!RC_IsFinite(value)) {
        return false;                  /* nan / inf 一律拒绝 */
    }

    *out = value;
    return true;
}

/* =========================================================================
 * CommandService_Init
 * ====================================================================== */
void CommandService_Init(void)
{
    s_pid_reset_request  = false;
    s_status_request     = false;
    s_send_reject_count  = 0U;
    s_parse_reject_count = 0U;
}

/* =========================================================================
 * CommandService_Parse
 * ====================================================================== */
RC_Result CommandService_Parse(const char *text, CommandMessage *out)
{
    char     tokens[RC_CMD_TOKEN_MAX][RC_CMD_TOKEN_LEN];
    uint8_t  count;
    float    value;

    if ((text == 0) || (out == 0)) {
        return RC_INVALID_PARAM;
    }

    count = CommandService_Tokenize(text, tokens);
    if ((count == 0U) || (count == CMD_TOKEN_ERR)) {
        return CommandService_ParseReject();
    }

    /* ---- 无参数命令：必须恰好 1 个 token ---- */
    if (count == 1U) {
        if (strcmp(tokens[0], "START") == 0) {
            out->type  = CMD_START;
            out->value = 0.0f;
        } else if (strcmp(tokens[0], "STOP") == 0) {
            out->type  = CMD_STOP;
            out->value = 0.0f;
        } else if (strcmp(tokens[0], "RESET") == 0) {
            out->type  = CMD_RESET;
            out->value = 0.0f;
        } else if (strcmp(tokens[0], "STATUS") == 0) {
            out->type  = CMD_STATUS;
            out->value = 0.0f;
        } else {
            return CommandService_ParseReject();
        }
    }
    /* ---- 带参数命令：必须恰好 2 个 token ---- */
    else if (count == 2U) {
        if (strcmp(tokens[0], "SET_SPEED") != 0) {
            return CommandService_ParseReject();
        }
        if (!CommandService_ParseFloat(tokens[1], &value)) {
            return CommandService_ParseReject();
        }
        out->type  = CMD_SET_SPEED;
        out->value = value;
    }
    else {
        /* 多余 token 一律判非法，避免 "START xxx" 被静默当成 START */
        return CommandService_ParseReject();
    }

    out->timestamp = RC_Port_GetTick();
    return RC_OK;
}

/* =========================================================================
 * CommandService_Send
 * ====================================================================== */
RC_Result CommandService_Send(const CommandMessage *msg)
{
    RC_Result rc;

    if (msg == 0) {
        return RC_INVALID_PARAM;
    }

    rc = RC_Port_CmdQueueSend(msg);

    /* ICD §15：Queue 满不得无限阻塞。这里是唯一的失败出口，必须计数，
     * 否则"命令丢了"这件事在系统里完全没有痕迹。 */
    if (rc == RC_BUSY) {
        ++s_send_reject_count;
    }

    return rc;
}

/* =========================================================================
 * CommandService_Handle
 * ====================================================================== */
void CommandService_Handle(const CommandMessage *msg)
{
    if (msg == 0) {
        return;
    }

    switch (msg->type) {
        case CMD_START:
            /* READY/STOPPED -> RUNNING 合法；INIT -> RUNNING 被 §10 拒绝 */
            (void)MotorManager_SetState(MOTOR_RUNNING);
            break;

        case CMD_STOP:
            (void)MotorManager_SetState(MOTOR_STOPPED);
            break;

        case CMD_SET_SPEED:
            /* 越界值由 MotorManager 返回 RC_INVALID_PARAM 并保持原目标不变 */
            (void)MotorManager_SetTargetSpeed(msg->value);
            break;

        case CMD_RESET:
            (void)MotorManager_RequestReset();
            s_pid_reset_request = true;    /* 由 ControlTask 执行实际的 PID_Reset */
            break;

        case CMD_STATUS:
            s_status_request = true;       /* 由 DebugTask 执行实际的打印 */
            break;

        case CMD_NONE:
        default:
            break;
    }
}

/* =========================================================================
 * 扩展访问器（CR-004）
 * ====================================================================== */
bool CommandService_TakePidResetRequest(void)
{
    bool requested = s_pid_reset_request;
    s_pid_reset_request = false;
    return requested;
}

bool CommandService_TakeStatusRequest(void)
{
    bool requested = s_status_request;
    s_status_request = false;
    return requested;
}

uint32_t CommandService_GetSendRejectCount(void)
{
    return s_send_reject_count;
}

uint32_t CommandService_GetParseRejectCount(void)
{
    return s_parse_reject_count;
}

/** @brief 内部：记一次解析失败（集中在一处，避免漏记某个 return 分支） */
static RC_Result CommandService_ParseReject(void)
{
    ++s_parse_reject_count;
    return RC_INVALID_PARAM;
}
