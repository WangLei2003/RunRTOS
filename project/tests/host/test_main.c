/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - host test process and production scheduler bootstrap
 *
 * Layer: tests/host
 * Purpose: Initialize the allocator/kernel and run all suites on host fibers.
 * Dependencies: Windows console output, RunRTOS public APIs, and test suites.
 * Constraint: The ticker is a host-only diagnostic aid; it drives production
 *             tick code but never replaces architecture context handling.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "test_harness.h"
#include "runrtos/core/run_config.h"
#include "runrtos/kernel/run_sync.h"
#include "runrtos/runtime/run_format.h"
#include "runrtos/runtime/run_memory.h"

#define RUN_TEST_ARENA_BYTES (512U * 1024U)
#define RUN_TEST_PRIORITY 20U
#define RUN_TEST_TICKER_PRIORITY 1U

RUN_STATIC RUN_ATTR_ALIGNED(8) RUN_UINT_8 RunTestArena[RUN_TEST_ARENA_BYTES];
RUN_STATIC RunMemory *RunTestMemory;
RUN_STATIC RunTestContext RunTests;
RUN_STATIC RunSemaphore *RunTestKick;

/* Writes a null-terminated test message to the host standard output. */
RUN_VOID run_test_output(RUN_CONST RUN_CHAR *runText)
{
    RUN_SIZE runLength = 0U;
    DWORD runWritten = 0U;
    RUN_CONST RUN_CHAR *runCursor = runText;
    while ((runCursor != RUN_NULL) && (*runCursor != '\0'))
    {
        runLength++;
        runCursor++;
    }
    if (runLength != 0U)
    {
        (RUN_VOID)WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), runText,
                            (DWORD)runLength, &runWritten, RUN_NULL);
    }
}

/* Formats and prints one failed test expression. */
RUN_VOID run_test_report_failure(RunTestContext *runContext,
                                 RUN_CONST RUN_CHAR *runExpression,
                                 RUN_UINT_32 runLine)
{
    RUN_CHAR runText[192];
    RUN_SIZE runLength = 0U;
    runContext->runFailures++;
    (RUN_VOID)run_format_buffer(runText, sizeof(runText), &runLength,
                                "FAIL %s:%u: %s\r\n",
                                runContext->runSuite, runLine, runExpression);
    run_test_output(runText);
}

/* Records one assertion result and reports failures without aborting the suite. */
RUN_VOID run_test_check(RunTestContext *runContext,
                        RUN_BOOL runCondition,
                        RUN_CONST RUN_CHAR *runExpression,
                        RUN_UINT_32 runLine)
{
    runContext->runChecks++;
    if (!runCondition)
    {
        run_test_report_failure(runContext, runExpression, runLine);
    }
}

/* Prints the standard banner for a host test suite. */
RUN_VOID run_test_suite_begin(RunTestContext *runContext,
                              RUN_CONST RUN_CHAR *runSuite)
{
    runContext->runSuite = runSuite;
    run_test_output("[ RUN      ] ");
    run_test_output(runSuite);
    run_test_output("\r\n");
}

/* Advances the production tick source from a low-priority host thread. */
RUN_STATIC RUN_VOID run_test_ticker(RUN_VOID *runArgument)
{
    (RUN_VOID)runArgument;
    for (;;)
    {
        run_kernel_tick_from_isr();
        run_scheduler_yield();
    }
}

/* Runs every host suite and exits with their aggregate failure status. */
RUN_ATTR_NORETURN RUN_STATIC RUN_VOID run_test_runner(RUN_VOID *runArgument)
{
    RUN_UINT_32 runFailures;
    RUN_UINT_32 runChecks;
    RUN_CHAR runText[160];
    RUN_SIZE runLength = 0U;
    (RUN_VOID)runArgument;

    run_test_runtime_suite(&RunTests);
    run_test_kernel_suite(&RunTests, RunTestKick);
    run_test_sync_suite(&RunTests, RunTestKick);
    run_test_driver_suite(&RunTests);

    runFailures = RunTests.runFailures;
    runChecks = RunTests.runChecks;
    (RUN_VOID)run_format_buffer(runText, sizeof(runText), &runLength,
                                "SUMMARY checks=%u failures=%u\r\n",
                                runChecks, runFailures);
    run_test_output(runText);
    run_test_output((runFailures == 0U) ? "PASS runrtos host tests\r\n" :
                                         "FAIL runrtos host tests\r\n");
    ExitProcess((runFailures == 0U) ? 0U : 1U);
}

/* Initializes host kernel services and launches the test scheduler. */
RUN_INT_32 main(RUN_VOID)
{
    RunThreadConfig runTickerConfig = {
        "host-ticker", run_test_ticker, RUN_NULL, 512U,
        RUN_TEST_TICKER_PRIORITY, 1U
    };
    RunThreadConfig runRunnerConfig = {
        "host-runner", run_test_runner, RUN_NULL, 8192U,
        RUN_TEST_PRIORITY, 1U
    };
    RunThread *runThread = RUN_NULL;
    RunStatus runStatus;

    RunTests.runSuite = "bootstrap";
    runStatus = run_memory_init(&RunTestMemory, RunTestArena, sizeof(RunTestArena));
    if (runStatus != RUN_STATUS_OK)
    {
        run_test_output("FAIL memory init\r\n");
        return 10;
    }
    runStatus = run_kernel_init(RunTestMemory);
    if (runStatus != RUN_STATUS_OK)
    {
        run_test_output("FAIL kernel init\r\n");
        return 11;
    }
    runStatus = run_timer_system_init();
    if (runStatus != RUN_STATUS_OK)
    {
        run_test_output("FAIL timer init\r\n");
        return 12;
    }
    runStatus = run_semaphore_create(0U, 1U, &RunTestKick);
    if (runStatus != RUN_STATUS_OK)
    {
        run_test_output("FAIL kick semaphore\r\n");
        return 13;
    }
    runStatus = run_thread_create(&runTickerConfig, &runThread);
    if (runStatus != RUN_STATUS_OK)
    {
        run_test_output("FAIL ticker create\r\n");
        return 14;
    }
    runStatus = run_thread_create(&runRunnerConfig, &runThread);
    if (runStatus != RUN_STATUS_OK)
    {
        run_test_output("FAIL runner create\r\n");
        return 15;
    }
    run_scheduler_start();
}
