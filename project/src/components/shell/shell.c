/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - UART command shell and standard diagnostics
 *
 * Layer: components/shell
 * Purpose: Parse bounded command lines and expose kernel, memory, demo, selftest,
 *          and stress operations through a dedicated scheduler thread.
 * Dependencies: board, UART, kernel snapshots, selftest, stress, and formatting.
 * Constraint: Input uses fixed buffers, permits eight arguments, and never waits
 *             for UART output outside normal thread context.
 */
#include "runrtos/components/shell/run_shell.h"
#include "runrtos/components/selftest/run_selftest.h"
#include "runrtos/components/stress/run_stress.h"
#include "runrtos/core/run_config.h"
#include "runrtos/runtime/run_format.h"

#define RUN_SHELL_LINE_CAPACITY 128U
#define RUN_SHELL_ARGUMENT_CAPACITY 8U
#define RUN_SHELL_DEMO_PERIOD_MS 1000U
#define RUN_SHELL_LITERAL(_runText) (_runText), (sizeof(_runText) - 1U)

typedef RUN_STRUCT RunShellState
{
    RunShellConfig runConfig;
    RunMemory *runMemory;
    RunMutex *runOutputMutex;
    RunThread *runThread;
    RunStress *runStress;
    RUN_UINT_32 runDemoSeconds;
    RunTimeMs runDemoNextMs;
    RUN_BOOL runDemoActive;
    RUN_BOOL runStarted;
} RunShellState;

typedef RUN_STRUCT RunShellFormatBuffer
{
    RUN_CHAR *runData;
    RUN_SIZE runCapacity;
    RUN_SIZE runLength;
    RUN_BOOL runTruncated;
} RunShellFormatBuffer;

/* Appends formatter output to the shell's bounded stack buffer. */
RUN_STATIC RUN_VOID run_shell_collect_format(RUN_CONST RUN_CHAR *runText,
                                             RUN_SIZE runTextLength,
                                             RUN_VOID *runContext)
{
    RunShellFormatBuffer *runBuffer = (RunShellFormatBuffer *)runContext;
    RUN_SIZE runAvailable = runBuffer->runCapacity - runBuffer->runLength;
    RUN_SIZE runCopy = (runTextLength < runAvailable) ?
        runTextLength : runAvailable;
    (RUN_VOID)run_memory_copy(&runBuffer->runData[runBuffer->runLength],
                              runText, runCopy);
    runBuffer->runLength += runCopy;
    if (runCopy != runTextLength)
    {
        runBuffer->runTruncated = RUN_TRUE;
    }
}

RUN_STATIC RUN_VOID run_shell_help_adapter(RunShell *runShell,
                                           RUN_UINT_32 runArgumentCount,
                                           RUN_CHAR *RUN_CONST *runArguments,
                                           RUN_VOID *runArgument);
RUN_STATIC RUN_VOID run_shell_threads_adapter(RunShell *runShell,
                                              RUN_UINT_32 runArgumentCount,
                                              RUN_CHAR *RUN_CONST *runArguments,
                                              RUN_VOID *runArgument);
RUN_STATIC RUN_VOID run_shell_mem_adapter(RunShell *runShell,
                                          RUN_UINT_32 runArgumentCount,
                                          RUN_CHAR *RUN_CONST *runArguments,
                                          RUN_VOID *runArgument);
RUN_STATIC RUN_VOID run_shell_timers_adapter(RunShell *runShell,
                                             RUN_UINT_32 runArgumentCount,
                                             RUN_CHAR *RUN_CONST *runArguments,
                                             RUN_VOID *runArgument);
RUN_STATIC RUN_VOID run_shell_uptime_adapter(RunShell *runShell,
                                             RUN_UINT_32 runArgumentCount,
                                             RUN_CHAR *RUN_CONST *runArguments,
                                             RUN_VOID *runArgument);
RUN_STATIC RUN_VOID run_shell_selftest_adapter(RunShell *runShell,
                                               RUN_UINT_32 runArgumentCount,
                                               RUN_CHAR *RUN_CONST *runArguments,
                                               RUN_VOID *runArgument);
RUN_STATIC RUN_VOID run_shell_stress_adapter(RunShell *runShell,
                                             RUN_UINT_32 runArgumentCount,
                                             RUN_CHAR *RUN_CONST *runArguments,
                                             RUN_VOID *runArgument);
RUN_STATIC RUN_VOID run_shell_demo_adapter(RunShell *runShell,
                                           RUN_UINT_32 runArgumentCount,
                                           RUN_CHAR *RUN_CONST *runArguments,
                                           RUN_VOID *runArgument);

RUN_STATIC RUN_CONST RunShellCommand RunShellDefaultCommands[] = {
    { "help", "help", "list commands", run_shell_help_adapter, RUN_NULL },
    { "threads", "threads", "show thread snapshots", run_shell_threads_adapter, RUN_NULL },
    { "mem", "mem", "show allocator statistics", run_shell_mem_adapter, RUN_NULL },
    { "timers", "timers", "show timer snapshots", run_shell_timers_adapter, RUN_NULL },
    { "uptime", "uptime", "show scheduler uptime", run_shell_uptime_adapter, RUN_NULL },
    { "selftest", "selftest", "run board self-test", run_shell_selftest_adapter, RUN_NULL },
    { "stress", "stress [seconds|stop]", "run/stop background stress", run_shell_stress_adapter, RUN_NULL },
    { "demo", "demo start|stop", "control heartbeat log", run_shell_demo_adapter, RUN_NULL }
};

/* Compares two null-terminated command tokens without depending on libc. */
RUN_STATIC RUN_BOOL run_shell_text_equal(RUN_CONST RUN_CHAR *runLeft,
                                         RUN_CONST RUN_CHAR *runRight)
{
    if ((runLeft == RUN_NULL) || (runRight == RUN_NULL))
    {
        return RUN_FALSE;
    }
    while ((*runLeft != '\0') && (*runLeft == *runRight))
    {
        runLeft++;
        runRight++;
    }
    return (*runLeft == *runRight) ? RUN_TRUE : RUN_FALSE;
}

/* Reports whether a command-line character separates arguments. */
RUN_STATIC RUN_BOOL run_shell_is_space(RUN_CHAR runCharacter)
{
    return ((runCharacter == ' ') || (runCharacter == '\t')) ?
        RUN_TRUE : RUN_FALSE;
}

/* Serializes a bounded byte range onto the console UART. */
RunStatus run_shell_write(RunShell *runShell,
                          RUN_CONST RUN_CHAR *runText,
                          RUN_SIZE runLength)
{
    RunShellState *runState = (RunShellState *)runShell;
    RunStatus runStatus;
    if ((runState == RUN_NULL) || (runText == RUN_NULL) ||
        (runLength == 0U))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    if (run_kernel_is_in_isr())
    {
        return RUN_STATUS_CONTEXT;
    }
    runStatus = run_mutex_lock(runState->runOutputMutex, RUN_TIME_FOREVER);
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_uart_write(runState->runConfig.runDevices->runUart1,
                                   (RUN_CONST RUN_UINT_8 *)runText,
                                   runLength, 1000U);
        (RUN_VOID)run_mutex_unlock(runState->runOutputMutex);
    }
    return runStatus;
}

/* Formats one line into a fixed stack buffer and writes it atomically. */
RunStatus run_shell_printf(RunShell *runShell,
                           RUN_CONST RUN_CHAR *runFormat,
                           ...)
{
    RUN_CHAR runBuffer[256];
    RunShellFormatBuffer runCollector;
    RunStatus runStatus;
    RUN_VA_LIST runArguments;
    if ((runShell == RUN_NULL) || (runFormat == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runCollector.runData = runBuffer;
    runCollector.runCapacity = sizeof(runBuffer) - 1U;
    runCollector.runLength = 0U;
    runCollector.runTruncated = RUN_FALSE;
    RUN_VA_START(runArguments, runFormat);
    runStatus = run_format(run_shell_collect_format, &runCollector,
                           runFormat, runArguments);
    RUN_VA_END(runArguments);
    runBuffer[runCollector.runLength] = '\0';
    if ((runStatus != RUN_STATUS_OK) || runCollector.runTruncated)
    {
        return (runStatus != RUN_STATUS_OK) ?
            runStatus : RUN_STATUS_OVERFLOW;
    }
    return run_shell_write(runShell, runBuffer, runCollector.runLength);
}

/* Adapts self-test output callbacks to the shell writer. */
RUN_STATIC RUN_VOID run_shell_selftest_write(RUN_CONST RUN_CHAR *runText,
                                             RUN_SIZE runLength,
                                             RUN_VOID *runContext)
{
    (RUN_VOID)run_shell_write((RunShell *)runContext, runText, runLength);
}

/* Prints the final stress report when the background worker finishes. */
RUN_STATIC RUN_VOID run_shell_stress_complete(
    RunStress *runStress,
    RUN_CONST RunStressInfo *runInfo,
    RUN_VOID *runArgument)
{
    RunShell *runShell = (RunShell *)runArgument;
    (RUN_VOID)runStress;
    (RUN_VOID)run_shell_printf(
        runShell,
        "stress done: iterations=%u failures=%u\r\n"
        "memory: baseline=%u used=%u peak=%u alloc-failures=%u\r\n"
        "stack: %u/%u bytes high-water\r\n",
        runInfo->runIterations, runInfo->runFailures,
        (RUN_UINT_32)runInfo->runMemoryBaseline,
        (RUN_UINT_32)runInfo->runMemoryUsed,
        (RUN_UINT_32)runInfo->runMemoryPeak,
        runInfo->runAllocatorFailures,
        (RUN_UINT_32)runInfo->runStackHighWater,
        (RUN_UINT_32)runInfo->runStackSize);
}

/* Writes the standard interactive shell prompt. */
RUN_STATIC RUN_VOID run_shell_prompt(RunShell *runShell)
{
    (RUN_VOID)run_shell_write(runShell, RUN_SHELL_LITERAL("runrtos> "));
}

/* Echoes one received input byte back to the console. */
RUN_STATIC RUN_VOID run_shell_echo(RunShell *runShell, RUN_CHAR runCharacter)
{
    (RUN_VOID)run_shell_write(runShell, &runCharacter, 1U);
}

/* Tokenizes and dispatches one bounded shell command line. */
RUN_STATIC RunStatus run_shell_execute_line(RunShell *runShell,
                                            RUN_CHAR *runLine)
{
    RunShellState *runState = (RunShellState *)runShell;
    RUN_CHAR *runArguments[RUN_SHELL_ARGUMENT_CAPACITY];
    RUN_UINT_32 runArgumentCount = 0U;
    RUN_SIZE runIndex = 0U;
    RUN_CONST RunShellCommand *runCommand = RUN_NULL;

    while ((runLine[runIndex] != '\0') && run_shell_is_space(runLine[runIndex]))
    {
        runIndex++;
    }
    while (runLine[runIndex] != '\0')
    {
        if (runArgumentCount == RUN_SHELL_ARGUMENT_CAPACITY)
        {
            (RUN_VOID)run_shell_write(
                runShell,
                RUN_SHELL_LITERAL("error: too many arguments\r\n"));
            return RUN_STATUS_FULL;
        }
        runArguments[runArgumentCount++] = &runLine[runIndex];
        while ((runLine[runIndex] != '\0') &&
               !run_shell_is_space(runLine[runIndex]))
        {
            runIndex++;
        }
        if (runLine[runIndex] != '\0')
        {
            runLine[runIndex++] = '\0';
            while ((runLine[runIndex] != '\0') &&
                   run_shell_is_space(runLine[runIndex]))
            {
                runIndex++;
            }
        }
    }
    if (runArgumentCount == 0U)
    {
        return RUN_STATUS_OK;
    }
    for (RUN_SIZE runCommandIndex = 0U;
         runCommandIndex < runState->runConfig.runCommandCount;
         runCommandIndex++)
    {
        if (run_shell_text_equal(runArguments[0],
                                 runState->runConfig.runCommands[runCommandIndex].runName))
        {
            runCommand = &runState->runConfig.runCommands[runCommandIndex];
            break;
        }
    }
    if ((runCommand == RUN_NULL) || (runCommand->runHandler == RUN_NULL))
    {
        (RUN_VOID)run_shell_printf(runShell, "unknown command: %s\r\n",
                                   runArguments[0]);
        return RUN_STATUS_NOT_FOUND;
    }
    runCommand->runHandler(runShell, runArgumentCount, runArguments,
                           runCommand->runArgument);
    return RUN_STATUS_OK;
}

/* Emits periodic demo output while the user has enabled it. */
RUN_STATIC RUN_VOID run_shell_demo_update(RunShell *runShell)
{
    RunShellState *runState = (RunShellState *)runShell;
    RunKernelStats runStats;
    RunTimeMs runNow;
    if (!runState->runDemoActive)
    {
        return;
    }
    run_kernel_get_stats(&runStats);
    runNow = (RunTimeMs)((runStats.runTicks * 1000U) / RUN_CONFIG_TICK_HZ);
    if (runNow >= runState->runDemoNextMs)
    {
        (RUN_VOID)run_gpio_toggle(runState->runConfig.runDevices->runLedPort, 0U);
        runState->runDemoSeconds++;
        (RUN_VOID)run_shell_printf(runShell, "RunRTOS heartbeat %u s\r\n",
                                   runState->runDemoSeconds);
        runState->runDemoNextMs = runNow + RUN_SHELL_DEMO_PERIOD_MS;
    }
}

/* Reads console input, echoes it, and executes complete command lines. */
RUN_STATIC RUN_VOID run_shell_thread(RUN_VOID *runArgument)
{
    RunShell *runShell = (RunShell *)runArgument;
    RunShellState *runState = (RunShellState *)runShell;
    RUN_CHAR runLine[RUN_SHELL_LINE_CAPACITY];
    RUN_SIZE runLineLength = 0U;
    RUN_BOOL runIgnoreLf = RUN_FALSE;

    (RUN_VOID)run_shell_write(
        runShell,
        RUN_SHELL_LITERAL("RunRTOS 0.1.0 - type help for commands\r\n"));
    run_shell_prompt(runShell);
    for (;;)
    {
        RUN_UINT_8 runByte = 0U;
        RUN_SIZE runRead = 0U;
        RunStatus runStatus = run_uart_read(
            runState->runConfig.runDevices->runUart1, &runByte, 1U,
            &runRead, runState->runConfig.runReadTimeout);
        if ((runStatus != RUN_STATUS_OK) || (runRead == 0U))
        {
            run_shell_demo_update(runShell);
            continue;
        }
        if (runByte == (RUN_UINT_8)'\n')
        {
            if (runIgnoreLf)
            {
                runIgnoreLf = RUN_FALSE;
                continue;
            }
        }
        else
        {
            runIgnoreLf = RUN_FALSE;
        }
        if ((runByte == (RUN_UINT_8)'\r') || (runByte == (RUN_UINT_8)'\n'))
        {
            (RUN_VOID)run_shell_write(runShell, RUN_SHELL_LITERAL("\r\n"));
            runLine[runLineLength] = '\0';
            (RUN_VOID)run_shell_execute_line(runShell, runLine);
            runLineLength = 0U;
            runIgnoreLf = (runByte == (RUN_UINT_8)'\r') ? RUN_TRUE : RUN_FALSE;
            run_shell_prompt(runShell);
        }
        else if ((runByte == (RUN_UINT_8)'\b') ||
                 (runByte == (RUN_UINT_8)0x7FU))
        {
            if (runLineLength != 0U)
            {
                runLineLength--;
                (RUN_VOID)run_shell_write(runShell,
                                          RUN_SHELL_LITERAL("\b \b"));
            }
        }
        else if ((runByte >= (RUN_UINT_8)' ') &&
                 (runByte <= (RUN_UINT_8)'~'))
        {
            if ((runLineLength + 1U) < sizeof(runLine))
            {
                runLine[runLineLength++] = (RUN_CHAR)runByte;
                run_shell_echo(runShell, (RUN_CHAR)runByte);
            }
            else
            {
                run_shell_echo(runShell, '\a');
            }
        }
    }
}

/* Implements the help command from the shell's read-only command table. */
RUN_STATIC RUN_VOID run_shell_help_adapter(RunShell *runShell,
                                           RUN_UINT_32 runArgumentCount,
                                           RUN_CHAR *RUN_CONST *runArguments,
                                           RUN_VOID *runArgument)
{
    RunShellState *runState = (RunShellState *)runShell;
    (RUN_VOID)runArgumentCount;
    (RUN_VOID)runArguments;
    (RUN_VOID)runArgument;
    for (RUN_SIZE runIndex = 0U;
         runIndex < runState->runConfig.runCommandCount; runIndex++)
    {
        RUN_CONST RunShellCommand *runCommand =
            &runState->runConfig.runCommands[runIndex];
        (RUN_VOID)run_shell_printf(runShell, "%s - %s\r\n",
                                   runCommand->runUsage,
                                   runCommand->runDescription);
    }
}

/* Implements the threads command using read-only scheduler snapshots. */
RUN_STATIC RUN_VOID run_shell_threads_adapter(
    RunShell *runShell,
    RUN_UINT_32 runArgumentCount,
    RUN_CHAR *RUN_CONST *runArguments,
    RUN_VOID *runArgument)
{
    RunThreadInfo runInfo;
    RUN_SIZE runIndex = 0U;
    (RUN_VOID)runArgumentCount;
    (RUN_VOID)runArguments;
    (RUN_VOID)runArgument;
    while (run_thread_get_info(runIndex, &runInfo) == RUN_STATUS_OK)
    {
        (RUN_VOID)run_shell_printf(runShell, "thread[%u] ",
                                   (RUN_UINT_32)runIndex);
        (RUN_VOID)run_shell_write(runShell,
                                  runInfo.runStats.runName.runData,
                                  runInfo.runStats.runName.runLength);
        (RUN_VOID)run_shell_printf(
            runShell, " prio=%u state=%u stack=%u/%u\r\n",
            runInfo.runStats.runPriority, runInfo.runStats.runState,
            (RUN_UINT_32)runInfo.runStats.runStackHighWater,
            (RUN_UINT_32)runInfo.runStats.runStackSize);
        runIndex++;
    }
}

/* Implements the mem command using allocator statistics. */
RUN_STATIC RUN_VOID run_shell_mem_adapter(RunShell *runShell,
                                          RUN_UINT_32 runArgumentCount,
                                          RUN_CHAR *RUN_CONST *runArguments,
                                          RUN_VOID *runArgument)
{
    RunShellState *runState = (RunShellState *)runShell;
    RunMemoryStats runStats;
    (RUN_VOID)runArgumentCount;
    (RUN_VOID)runArguments;
    (RUN_VOID)runArgument;
    run_memory_get_stats(runState->runMemory, &runStats);
    (RUN_VOID)run_shell_printf(
        runShell,
        "memory: capacity=%u used=%u peak=%u largest=%u\r\n"
        "alloc=%u free=%u failures=%u check=%s\r\n",
        (RUN_UINT_32)runStats.runCapacity,
        (RUN_UINT_32)runStats.runUsed,
        (RUN_UINT_32)runStats.runPeakUsed,
        (RUN_UINT_32)runStats.runLargestFree,
        runStats.runAllocationCount, runStats.runFreeCount,
        runStats.runFailureCount,
        run_status_name(run_memory_check(runState->runMemory)));
}

/* Implements the timers command using read-only timer snapshots. */
RUN_STATIC RUN_VOID run_shell_timers_adapter(
    RunShell *runShell,
    RUN_UINT_32 runArgumentCount,
    RUN_CHAR *RUN_CONST *runArguments,
    RUN_VOID *runArgument)
{
    RunTimerInfo runInfo;
    RUN_SIZE runIndex = 0U;
    (RUN_VOID)runArgumentCount;
    (RUN_VOID)runArguments;
    (RUN_VOID)runArgument;
    while (run_timer_get_info(runIndex, &runInfo) == RUN_STATUS_OK)
    {
        (RUN_VOID)run_shell_printf(
            runShell, "timer[%u] state=%u periodic=%u remaining=%u ms callbacks=%u\r\n",
            (RUN_UINT_32)runIndex, runInfo.runState,
            runInfo.runPeriodic ? 1U : 0U,
            (RUN_UINT_32)runInfo.runRemaining,
            (RUN_UINT_32)runInfo.runCallbackCount);
        runIndex++;
    }
    if (runIndex == 0U)
    {
        (RUN_VOID)run_shell_write(runShell,
                                  RUN_SHELL_LITERAL("no timers\r\n"));
    }
}

/* Implements the uptime command from the scheduler tick snapshot. */
RUN_STATIC RUN_VOID run_shell_uptime_adapter(
    RunShell *runShell,
    RUN_UINT_32 runArgumentCount,
    RUN_CHAR *RUN_CONST *runArguments,
    RUN_VOID *runArgument)
{
    RunKernelStats runStats;
    RUN_UINT_64 runMilliseconds;
    (RUN_VOID)runArgumentCount;
    (RUN_VOID)runArguments;
    (RUN_VOID)runArgument;
    run_kernel_get_stats(&runStats);
    runMilliseconds = (runStats.runTicks * 1000U) / RUN_CONFIG_TICK_HZ;
    (RUN_VOID)run_shell_printf(
        runShell, "uptime: %u s %u ms (%u ticks), switches=%u\r\n",
        (RUN_UINT_32)(runMilliseconds / 1000U),
        (RUN_UINT_32)(runMilliseconds % 1000U),
        (RUN_UINT_32)runStats.runTicks,
        (RUN_UINT_32)runStats.runContextSwitches);
}

/* Implements selftest by invoking the board verification component. */
RUN_STATIC RUN_VOID run_shell_selftest_adapter(
    RunShell *runShell,
    RUN_UINT_32 runArgumentCount,
    RUN_CHAR *RUN_CONST *runArguments,
    RUN_VOID *runArgument)
{
    RunShellState *runState = (RunShellState *)runShell;
    RunStatus runStatus;
    (RUN_VOID)runArgumentCount;
    (RUN_VOID)runArguments;
    (RUN_VOID)runArgument;
    runStatus = run_selftest_execute(runState->runConfig.runDevices,
                                     run_shell_selftest_write, runShell);
    (RUN_VOID)run_shell_printf(runShell, "selftest: %s\r\n",
                               run_status_name(runStatus));
}

/* Lazily creates the stress harness owned by this shell. */
RUN_STATIC RunStatus run_shell_ensure_stress(RunShell *runShell)
{
    RunShellState *runState = (RunShellState *)runShell;
    RunStressConfig runConfig;
    if (runState->runStress != RUN_NULL)
    {
        return RUN_STATUS_OK;
    }
    runConfig.runMemory = runState->runMemory;
    runConfig.runStackSize = 1024U;
    runConfig.runPriority = 2U;
    runConfig.runComplete = run_shell_stress_complete;
    runConfig.runCompleteArgument = runShell;
    return run_stress_create(&runConfig, &runState->runStress);
}

/* Implements starting, stopping, and reporting the stress workload. */
RUN_STATIC RUN_VOID run_shell_stress_adapter(
    RunShell *runShell,
    RUN_UINT_32 runArgumentCount,
    RUN_CHAR *RUN_CONST *runArguments,
    RUN_VOID *runArgument)
{
    RunShellState *runState = (RunShellState *)runShell;
    RunStatus runStatus;
    (RUN_VOID)runArgument;
    runStatus = run_shell_ensure_stress(runShell);
    if (runStatus != RUN_STATUS_OK)
    {
        (RUN_VOID)run_shell_printf(runShell, "stress: %s\r\n",
                                   run_status_name(runStatus));
        return;
    }
    if ((runArgumentCount >= 2U) &&
        run_shell_text_equal(runArguments[1], "stop"))
    {
        runStatus = run_stress_stop(runState->runStress);
        (RUN_VOID)run_shell_printf(runShell, "stress stop: %s\r\n",
                                   run_status_name(runStatus));
        return;
    }
    {
        RUN_UINT_32 runSeconds = 60U;
        if ((runArgumentCount >= 2U) &&
            !run_format_parse_u32(runArguments[1], &runSeconds))
        {
            (RUN_VOID)run_shell_write(
                runShell,
                RUN_SHELL_LITERAL("usage: stress [seconds|stop]\r\n"));
            return;
        }
        runStatus = run_stress_start(runState->runStress, runSeconds);
        (RUN_VOID)run_shell_printf(runShell, "stress start (%u s): %s\r\n",
                                   runSeconds, run_status_name(runStatus));
    }
}

/* Implements the demo start and demo stop subcommands. */
RUN_STATIC RUN_VOID run_shell_demo_adapter(
    RunShell *runShell,
    RUN_UINT_32 runArgumentCount,
    RUN_CHAR *RUN_CONST *runArguments,
    RUN_VOID *runArgument)
{
    RunShellState *runState = (RunShellState *)runShell;
    RunKernelStats runStats;
    (RUN_VOID)runArgument;
    if (runArgumentCount != 2U)
    {
        (RUN_VOID)run_shell_write(
            runShell, RUN_SHELL_LITERAL("usage: demo start|stop\r\n"));
        return;
    }
    if (run_shell_text_equal(runArguments[1], "start"))
    {
        runState->runDemoActive = RUN_TRUE;
        runState->runDemoSeconds = 0U;
        run_kernel_get_stats(&runStats);
        runState->runDemoNextMs =
            (RunTimeMs)((runStats.runTicks * 1000U) / RUN_CONFIG_TICK_HZ);
        (RUN_VOID)run_shell_write(runShell,
                                  RUN_SHELL_LITERAL("demo started\r\n"));
    }
    else if (run_shell_text_equal(runArguments[1], "stop"))
    {
        runState->runDemoActive = RUN_FALSE;
        (RUN_VOID)run_gpio_write(runState->runConfig.runDevices->runLedPort,
                                 0U, RUN_TRUE);
        (RUN_VOID)run_shell_write(runShell,
                                  RUN_SHELL_LITERAL("demo stopped\r\n"));
    }
    else
    {
        (RUN_VOID)run_shell_write(
            runShell, RUN_SHELL_LITERAL("usage: demo start|stop\r\n"));
    }
}

/* Allocates shell state, its synchronization primitive, and input buffers. */
RunStatus run_shell_create(RUN_CONST RunShellConfig *runConfig,
                           RunShell **runShellOut)
{
    RunShellState *runState;
    RunStatus runStatus;
    if ((runConfig == RUN_NULL) || (runShellOut == RUN_NULL) ||
        (runConfig->runDevices == RUN_NULL) ||
        (runConfig->runDevices->runMemory == RUN_NULL) ||
        (runConfig->runDevices->runUart1 == RUN_NULL) ||
        (runConfig->runCommands == RUN_NULL) ||
        (runConfig->runCommandCount == 0U) ||
        (runConfig->runStackSize < 1024U) ||
        (runConfig->runPriority == 0U) ||
        (runConfig->runReadTimeout == 0U))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    *runShellOut = RUN_NULL;
    runStatus = run_memory_allocate(runConfig->runDevices->runMemory,
                                    sizeof(RunShellState),
                                    RUN_MEMORY_ALIGNMENT,
                                    (RUN_VOID **)&runState);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    run_memory_set(runState, 0, sizeof(RunShellState));
    runState->runConfig = *runConfig;
    runState->runMemory = runConfig->runDevices->runMemory;
    runStatus = run_mutex_create(&(RunMutexConfig){ RUN_FALSE },
                                 &runState->runOutputMutex);
    if (runStatus != RUN_STATUS_OK)
    {
        (RUN_VOID)run_memory_release(runState->runMemory, runState);
        return runStatus;
    }
    *runShellOut = (RunShell *)runState;
    return RUN_STATUS_OK;
}

/* Creates the shell worker thread and enables command processing. */
RunStatus run_shell_start(RunShell *runShell)
{
    RunShellState *runState = (RunShellState *)runShell;
    RunThreadConfig runThreadConfig;
    RunStatus runStatus;
    if ((runState == RUN_NULL) || run_kernel_is_in_isr())
    {
        return (runState == RUN_NULL) ?
            RUN_STATUS_INVALID_ARGUMENT : RUN_STATUS_CONTEXT;
    }
    if (runState->runStarted)
    {
        return RUN_STATUS_ALREADY_EXISTS;
    }
    runThreadConfig.runName = runState->runConfig.runName;
    runThreadConfig.runEntry = run_shell_thread;
    runThreadConfig.runArgument = runShell;
    runThreadConfig.runStackSize = runState->runConfig.runStackSize;
    runThreadConfig.runPriority = runState->runConfig.runPriority;
    runThreadConfig.runTimeSliceTicks = runState->runConfig.runTimeSliceTicks;
    runStatus = run_thread_create(&runThreadConfig, &runState->runThread);
    if (runStatus == RUN_STATUS_OK)
    {
        runState->runStarted = RUN_TRUE;
    }
    return runStatus;
}

/* Stops the shell and releases all objects allocated by run_shell_create. */
RunStatus run_shell_delete(RunShell *runShell)
{
    RunShellState *runState = (RunShellState *)runShell;
    if ((runState == RUN_NULL) || run_kernel_is_in_isr())
    {
        return (runState == RUN_NULL) ?
            RUN_STATUS_INVALID_ARGUMENT : RUN_STATUS_CONTEXT;
    }
    if (runState->runThread != RUN_NULL)
    {
        (RUN_VOID)run_thread_delete(runState->runThread);
    }
    if (runState->runStress != RUN_NULL)
    {
        RunStatus runStatus = run_stress_delete(runState->runStress);
        if (runStatus != RUN_STATUS_OK)
        {
            return runStatus;
        }
    }
    (RUN_VOID)run_mutex_delete(runState->runOutputMutex);
    return run_memory_release(runState->runMemory, runState);
}

/* Returns the immutable built-in command table and its entry count. */
RunStatus run_shell_get_default_commands(RUN_CONST RunShellCommand **runCommandsOut,
                                         RUN_SIZE *runCommandCountOut)
{
    if ((runCommandsOut == RUN_NULL) || (runCommandCountOut == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    *runCommandsOut = RunShellDefaultCommands;
    *runCommandCountOut = RUN_ARRAY_COUNT(RunShellDefaultCommands);
    return RUN_STATUS_OK;
}
