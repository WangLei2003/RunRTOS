/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - F103 scheduler, timer, UART, and LED demo
 *
 * Layer: applications/demo
 * Purpose: Assemble board drivers and exercise the initial kernel feature set.
 * Dependencies: public board, kernel, sync, GPIO, UART, and formatting APIs.
 * Constraint: The timer callback only increments shared state; UART output and
 *             LED work run in thread context.
 */
#include "runrtos/applications/run_demo.h"
#include "runrtos/bsp/run_board.h"
#include "runrtos/core/run_assert.h"
#include "runrtos/kernel/run_kernel.h"
#include "runrtos/kernel/run_sync.h"
#include "runrtos/runtime/run_format.h"

typedef RUN_STRUCT RunDemoContext
{
    RunBoardDevices runDevices;
    RunTimer *runTimer;
    RUN_UINT_32 runTimerTicks;
} RunDemoContext;

RUN_STATIC RunDemoContext RunDemo;

RUN_STATIC RUN_VOID run_demo_timer(RunTimer *runTimer, RUN_VOID *runArgument)
{
    RunDemoContext *runContext = (RunDemoContext *)runArgument;
    (RUN_VOID)runTimer;
    runContext->runTimerTicks++;
}

RUN_STATIC RUN_VOID run_demo_heartbeat(RUN_VOID *runArgument)
{
    RunDemoContext *runContext = (RunDemoContext *)runArgument;
    RUN_UINT_64 runSeconds = 0U;
    for (;;)
    {
        RUN_CHAR runText[64];
        RUN_SIZE runLength;
        (RUN_VOID)run_gpio_toggle(runContext->runDevices.runLedPort, 0U);
        (RUN_VOID)run_format_buffer(runText, sizeof(runText), &runLength,
                                    "RunRTOS heartbeat %u s, timer %u\r\n",
                                    (RUN_UINT_32)runSeconds,
                                    runContext->runTimerTicks);
        (RUN_VOID)run_uart_write(runContext->runDevices.runUart1,
                                 (RUN_CONST RUN_UINT_8 *)runText,
                                 runLength, 100U);
        runSeconds++;
        (RUN_VOID)run_thread_sleep(1000U);
    }
}

RUN_STATIC RUN_VOID run_demo_scheduler_probe(RUN_VOID *runArgument)
{
    (RUN_VOID)runArgument;
    for (;;)
    {
        (RUN_VOID)run_thread_sleep(250U);
    }
}

RUN_ATTR_NORETURN RUN_VOID run_application_start(RUN_VOID)
{
    RunThreadConfig runConfig = {
        "heartbeat",
        run_demo_heartbeat,
        &RunDemo,
        768U,
        2U,
        10U
    };
    RunThread *runThread;
    RunStatus runStatus;

    runStatus = run_board_init(&RunDemo.runDevices);
    if (runStatus != RUN_STATUS_OK)
    {
        run_panic("board init", __FILE__, (RUN_UINT_32)__LINE__, runStatus);
    }
    runStatus = run_timer_system_init();
    if (runStatus != RUN_STATUS_OK)
    {
        run_panic("timer init", __FILE__, (RUN_UINT_32)__LINE__, runStatus);
    }
    runStatus = run_timer_create(run_demo_timer, &RunDemo, RUN_TRUE, 1000U,
                                 &RunDemo.runTimer);
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_timer_start(RunDemo.runTimer, 1000U);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_thread_create(&runConfig, &runThread);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runConfig.runName = "probe";
        runConfig.runEntry = run_demo_scheduler_probe;
        runConfig.runStackSize = 512U;
        runConfig.runPriority = 2U;
        runStatus = run_thread_create(&runConfig, &runThread);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_board_start_tick();
    }
    if (runStatus != RUN_STATUS_OK)
    {
        run_panic("application init", __FILE__, (RUN_UINT_32)__LINE__, runStatus);
    }
    run_scheduler_start();
}

