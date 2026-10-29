/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - F103RB Shell application
 *
 * Layer: applications/demo
 * Purpose: Initialize the board, timer service, Shell, and scheduler.
 * Dependencies: board, kernel, sync, shell, and formatting interfaces.
 * Constraint: Default boot prints only the banner and prompt; heartbeat output
 *             starts only after the user issues demo start.
 */
#include "runrtos/applications/run_demo.h"
#include "runrtos/bsp/run_board.h"
#include "runrtos/core/run_assert.h"
#include "runrtos/core/run_config.h"
#include "runrtos/kernel/run_sync.h"
#if RUN_CONFIG_SHELL
#include "runrtos/components/shell/run_shell.h"
#endif

typedef RUN_STRUCT RunDemoContext
{
    RunBoardDevices runDevices;
#if RUN_CONFIG_SHELL
    RunShell *runShell;
#endif
} RunDemoContext;

RUN_STATIC RunDemoContext RunDemo;

/* Initializes board services, starts the Shell, and transfers control to the scheduler. */
RUN_ATTR_NORETURN RUN_VOID run_application_start(RUN_VOID)
{
    RunStatus runStatus;
#if RUN_CONFIG_SHELL
    RUN_CONST RunShellCommand *runCommands;
    RUN_SIZE runCommandCount;
    RunShellConfig runShellConfig;
#endif

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
#if RUN_CONFIG_SHELL
    runStatus = run_shell_get_default_commands(&runCommands, &runCommandCount);
    if (runStatus == RUN_STATUS_OK)
    {
        runShellConfig.runDevices = &RunDemo.runDevices;
        runShellConfig.runName = "shell";
        runShellConfig.runStackSize = 2048U;
        runShellConfig.runPriority = 3U;
        runShellConfig.runTimeSliceTicks = 2U;
        runShellConfig.runReadTimeout = 100U;
        runShellConfig.runCommands = runCommands;
        runShellConfig.runCommandCount = runCommandCount;
        runStatus = run_shell_create(&runShellConfig, &RunDemo.runShell);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_shell_start(RunDemo.runShell);
    }
#endif
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
