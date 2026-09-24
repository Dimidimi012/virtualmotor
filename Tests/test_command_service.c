/**
 * @file    test_command_service.c
 * @brief   Service/CommandService 单元测试 —— ICD §11 API / §15 命令集 / §21 错误策略
 */

#include "test_framework.h"
#include "command_service.h"
#include "motor_manager.h"
#include "config.h"
#include "rc_port.h"
#include "sim_rtos.h"

static void reset_all(void)
{
    sim_rtos_reset();
    MotorManager_Init();
    CommandService_Init();
    RC_Port_CmdQueueCreate();
}

void TestSuite_CommandService(void)
{
    CommandMessage msg;

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Parse 无参数命令（ICD §15 推荐命令集）");
    {
        reset_all();
        TC_CHECK(CommandService_Parse("START", &msg) == RC_OK);
        TC_CHECK(msg.type == CMD_START);
        TC_CHECK(CommandService_Parse("STOP", &msg) == RC_OK);
        TC_CHECK(msg.type == CMD_STOP);
        TC_CHECK(CommandService_Parse("RESET", &msg) == RC_OK);
        TC_CHECK(msg.type == CMD_RESET);
        TC_CHECK(CommandService_Parse("STATUS", &msg) == RC_OK);
        TC_CHECK(msg.type == CMD_STATUS);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Parse SET_SPEED 与参数值");
    {
        reset_all();
        TC_CHECK(CommandService_Parse("SET_SPEED 1000", &msg) == RC_OK);
        TC_CHECK(msg.type == CMD_SET_SPEED);
        TC_NEAR(msg.value, 1000.0f, 1e-4);

        TC_CHECK(CommandService_Parse("SET_SPEED -250.5", &msg) == RC_OK);
        TC_NEAR(msg.value, -250.5f, 1e-4);

        TC_CHECK(CommandService_Parse("set_speed 42", &msg) == RC_OK);   /* 大小写不敏感 */
        TC_NEAR(msg.value, 42.0f, 1e-4);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Parse 容错：多余空白、逗号、CRLF 结尾");
    {
        reset_all();
        TC_CHECK(CommandService_Parse("   START   ", &msg) == RC_OK);
        TC_CHECK(msg.type == CMD_START);

        TC_CHECK(CommandService_Parse("START\r\n", &msg) == RC_OK);
        TC_CHECK(msg.type == CMD_START);

        TC_CHECK(CommandService_Parse("\tSET_SPEED\t500\t\r\n", &msg) == RC_OK);
        TC_NEAR(msg.value, 500.0f, 1e-4);

        TC_CHECK(CommandService_Parse("SET_SPEED,700", &msg) == RC_OK);
        TC_NEAR(msg.value, 700.0f, 1e-4);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Parse 拒绝非法输入（ICD §21 -> RC_INVALID_PARAM）");
    {
        reset_all();
        TC_CHECK(CommandService_Parse("", &msg)                == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("   \r\n", &msg)         == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("FOO", &msg)             == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("STARTX", &msg)          == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("SET_SPEED", &msg)       == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("SET_SPEED abc", &msg)   == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("SET_SPEED 1000abc", &msg) == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("SET_SPEED 1 2", &msg)   == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("START extra", &msg)     == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("SET_SPEED nan", &msg)   == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("SET_SPEED inf", &msg)   == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("AAAAAAAAAAAAAAAAAAAAAAAA", &msg) == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse(0, &msg)                 == RC_INVALID_PARAM);
        TC_CHECK(CommandService_Parse("START", 0)              == RC_INVALID_PARAM);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Parse 打时间戳（ICD §5 CommandMessage.timestamp）");
    {
        reset_all();
        sim_rtos_advance_tick(123U);
        TC_CHECK(CommandService_Parse("START", &msg) == RC_OK);
        TC_CHECK(msg.timestamp == 123U);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Send 入队；队列满返回 RC_BUSY 并计数（ICD §15 / §21）");
    {
        reset_all();
        TC_CHECK(CommandService_GetSendRejectCount() == 0U);

        /* 队列深度 RC_CMD_QUEUE_LENGTH = 8；填满它 */
        for (uint32_t i = 0U; i < (uint32_t)RC_CMD_QUEUE_LENGTH; ++i) {
            TC_CHECK(CommandService_Parse("SET_SPEED 100", &msg) == RC_OK);
            TC_CHECK(CommandService_Send(&msg) == RC_OK);
        }

        /* 第 9 条必须立即失败，而不是无限阻塞 */
        TC_CHECK(CommandService_Parse("STOP", &msg) == RC_OK);
        TC_CHECK(CommandService_Send(&msg) == RC_BUSY);
        TC_CHECK(CommandService_GetSendRejectCount() == 1U);

        /* 再发一条，计数继续累加 */
        TC_CHECK(CommandService_Send(&msg) == RC_BUSY);
        TC_CHECK(CommandService_GetSendRejectCount() == 2U);

        TC_CHECK(CommandService_Send(0) == RC_INVALID_PARAM);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Handle: START / STOP / SET_SPEED 通过 MotorManager 生效（ICD §10 / §13）");
    {
        reset_all();
        TC_CHECK(MotorManager_SetState(MOTOR_READY) == RC_OK);

        msg.type = CMD_START; msg.value = 0.0f; msg.timestamp = 0U;
        CommandService_Handle(&msg);
        TC_CHECK(MotorManager_GetState() == MOTOR_RUNNING);

        msg.type = CMD_SET_SPEED; msg.value = 1000.0f;
        CommandService_Handle(&msg);
        TC_NEAR(MotorManager_GetTargetSpeed(), 1000.0f, 1e-6);

        msg.type = CMD_STOP;
        CommandService_Handle(&msg);
        TC_CHECK(MotorManager_GetState() == MOTOR_STOPPED);   /* RUNNING -> STOPPED 合法 */
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Handle: SET_SPEED 越界不改变原目标（ICD §21）");
    {
        reset_all();
        (void)MotorManager_SetState(MOTOR_READY);
        msg.type = CMD_SET_SPEED; msg.value = 1000.0f;
        CommandService_Handle(&msg);

        msg.value = RC_MAX_TARGET_SPEED + 500.0f;
        CommandService_Handle(&msg);
        TC_NEAR(MotorManager_GetTargetSpeed(), 1000.0f, 1e-6);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Handle: RESET 产生 PID 复位请求并让状态回到 READY（ICD §27）");
    {
        reset_all();
        (void)MotorManager_SetState(MOTOR_READY);
        (void)MotorManager_SetState(MOTOR_RUNNING);
        (void)MotorManager_SetTargetSpeed(1000.0f);

        TC_CHECK(CommandService_TakePidResetRequest() == false);
        msg.type = CMD_RESET; msg.value = 0.0f;
        CommandService_Handle(&msg);

        TC_CHECK(CommandService_TakePidResetRequest() == true);
        TC_CHECK(CommandService_TakePidResetRequest() == false);   /* 读后清零 */
        TC_CHECK(MotorManager_GetState() == MOTOR_READY);
        TC_NEAR(MotorManager_GetTargetSpeed(), 0.0f, 1e-9);
        TC_CHECK(MotorManager_TakeResetRequest() == true);         /* 通知 MotorTask */
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Handle: STATUS 产生立即打印请求（ICD §13 禁止 ControlTask printf）");
    {
        reset_all();
        TC_CHECK(CommandService_TakeStatusRequest() == false);
        msg.type = CMD_STATUS; msg.value = 0.0f;
        CommandService_Handle(&msg);
        TC_CHECK(CommandService_TakeStatusRequest() == true);
        TC_CHECK(CommandService_TakeStatusRequest() == false);
    }

    /* ------------------------------------------------------------------ */
    TC_BEGIN("Handle 空指针与 CMD_NONE 必须安全");
    {
        reset_all();
        CommandService_Handle(0);
        msg.type = CMD_NONE; msg.value = 0.0f;
        CommandService_Handle(&msg);
        msg.type = (CommandType)999;
        CommandService_Handle(&msg);   /* 未知类型不得崩溃 */
        TC_CHECK(1);
    }

    TC_SUITE_REPORT("CommandService");
}
