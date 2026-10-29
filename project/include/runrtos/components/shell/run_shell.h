/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - diagnostic command shell
 *
 * Layer: components/shell
 * Purpose: Provide a threaded, bounded command line over a UART device.
 * Dependencies: public board, driver, kernel, and runtime interfaces.
 * Constraint: One Shell owns one UART. Commands use a borrowed static table,
 *             accept at most eight whitespace-separated arguments, and support
 *             neither quoting nor escape processing.
 */
#ifndef RUN_SHELL_H_
#define RUN_SHELL_H_

#include "runrtos/bsp/run_board.h"

typedef RUN_STRUCT RunShell RunShell;

typedef RUN_VOID (*RunShellCommandHandler)(RunShell *runShell,
                                          RUN_UINT_32 runArgumentCount,
                                          RUN_CHAR *RUN_CONST *runArguments,
                                          RUN_VOID *runArgument);

typedef RUN_STRUCT RunShellCommand
{
    RUN_CONST RUN_CHAR *runName;
    RUN_CONST RUN_CHAR *runUsage;
    RUN_CONST RUN_CHAR *runDescription;
    RunShellCommandHandler runHandler;
    RUN_VOID *runArgument;
} RunShellCommand;

typedef RUN_STRUCT RunShellConfig
{
    RunBoardDevices *runDevices;
    RUN_CONST RUN_CHAR *runName;
    RUN_SIZE runStackSize;
    RUN_UINT_8 runPriority;
    RUN_UINT_32 runTimeSliceTicks;
    RunTimeMs runReadTimeout;
    /* The array and its handler arguments must remain valid for shell lifetime. */
    RUN_CONST RunShellCommand *runCommands;
    RUN_SIZE runCommandCount;
} RunShellConfig;

RunStatus run_shell_create(RUN_CONST RunShellConfig *runConfig,
                           RunShell **runShellOut);
/* Creates the independent shell thread; call before run_scheduler_start. */
RunStatus run_shell_start(RunShell *runShell);
/* Deletes the shell thread and its owned stress service and synchronization. */
RunStatus run_shell_delete(RunShell *runShell);

/* Thread-context output is serialized across Shell, selftest, and stress. */
RunStatus run_shell_write(RunShell *runShell,
                          RUN_CONST RUN_CHAR *runText,
                          RUN_SIZE runLength);
RunStatus run_shell_printf(RunShell *runShell,
                           RUN_CONST RUN_CHAR *runFormat,
                           ...);

/* Borrowed read-only table containing the eight standard 0.1 commands. */
RunStatus run_shell_get_default_commands(RUN_CONST RunShellCommand **runCommandsOut,
                                         RUN_SIZE *runCommandCountOut);

#endif
