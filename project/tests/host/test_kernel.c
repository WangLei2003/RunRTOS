/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - scheduler and thread lifecycle tests
 *
 * Layer: tests/host
 * Purpose: Exercise production scheduling, blocking, timeout, wrap, and deletion.
 * Dependencies: host Fiber port, private tick state for wrap injection, and public
 *               scheduler APIs.
 * Constraint: All worker state is static so pointers remain valid across fibers.
 */
#include "test_harness.h"
#include "run_kernel_internal.h"
#include "runrtos/kernel/run_sync.h"
#include "runrtos/runtime/run_format.h"

RUN_STATIC RUN_CHAR RunKernelTrace[32];
RUN_STATIC RUN_UINT_32 RunKernelTraceLength;
RUN_STATIC RunSemaphore *RunKernelDone;

/* Appends one scheduler ordering marker to the shared test trace. */
RUN_STATIC RUN_VOID run_kernel_trace(RUN_CHAR runValue)
{
    if (RunKernelTraceLength < sizeof(RunKernelTrace))
    {
        RunKernelTrace[RunKernelTraceLength++] = runValue;
    }
}

/* Emits a marker that reveals preemption by a higher-priority thread. */
RUN_STATIC RUN_VOID run_kernel_priority_worker(RUN_VOID *runArgument)
{
    (RUN_VOID)runArgument;
    run_kernel_trace('H');
}

/* Emits repeated markers to exercise equal-priority round-robin scheduling. */
RUN_STATIC RUN_VOID run_kernel_round_worker(RUN_VOID *runArgument)
{
    RUN_CHAR runValue = *(RUN_CONST RUN_CHAR *)runArgument;
    for (RUN_UINT_32 runIndex = 0U; runIndex < 2U; runIndex++)
    {
        run_kernel_trace(runValue);
        run_scheduler_yield();
    }
    (RUN_VOID)run_semaphore_give(RunKernelDone);
}

/* Burns one time slice and records a tick-driven rotation marker. */
RUN_STATIC RUN_VOID run_kernel_slice_worker(RUN_VOID *runArgument)
{
    RUN_CHAR runValue = *(RUN_CONST RUN_CHAR *)runArgument;
    for (RUN_UINT_32 runIndex = 0U; runIndex < 4U; runIndex++)
    {
        run_kernel_trace(runValue);
        /* Calling the production tick path forces the one-tick time slice. */
        run_kernel_tick_from_isr();
    }
    (RUN_VOID)run_semaphore_give(RunKernelDone);
}

/* Waits until the test suite resumes the worker thread. */
RUN_STATIC RUN_VOID run_kernel_resume_worker(RUN_VOID *runArgument)
{
    RUN_UINT_32 *runCounter = (RUN_UINT_32 *)runArgument;
    (*runCounter)++;
    (RUN_VOID)run_semaphore_give(RunKernelDone);
}

/* Provides a thread that immediately returns to test lifecycle cleanup. */
RUN_STATIC RUN_VOID run_kernel_noop_worker(RUN_VOID *runArgument)
{
    (RUN_VOID)runArgument;
}

/* Holds forever to test deletion of a blocked thread. */
RUN_STATIC RUN_VOID run_kernel_forever_worker(RUN_VOID *runArgument)
{
    RunSemaphore *runSemaphore = (RunSemaphore *)runArgument;
    (RUN_VOID)run_semaphore_take(runSemaphore, RUN_TIME_FOREVER);
}

/* Exercises scheduler priority, slicing, sleep, suspend, and deletion behavior. */
RUN_VOID run_test_kernel_suite(RunTestContext *runContext,
                               RunSemaphore *runKickSemaphore)
{
    RunThreadConfig runConfig = {
        "worker", RUN_NULL, RUN_NULL, 1024U, 5U, 1U
    };
    RunThread *runThread = RUN_NULL;
    RunThreadInfo runInfo;
    RunKernelStats runKernelStats;
    RunStatus runStatus;
    RUN_UINT_64 runBefore;
    RUN_UINT_32 runCounter = 0U;
    RUN_CHAR runFirstId = 'A';
    RUN_CHAR runSecondId = 'B';
    RunSemaphore *runForever = RUN_NULL;

    run_test_suite_begin(runContext, "kernel");
    RUN_TEST_STATUS(runContext, run_semaphore_create(0U, 2U, &RunKernelDone),
                    RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_semaphore_give(runKickSemaphore),
                    RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(runKickSemaphore, RUN_TIME_NO_WAIT),
                    RUN_STATUS_OK);

    RunKernelTraceLength = 0U;
    runConfig.runName = "high";
    runConfig.runEntry = run_kernel_priority_worker;
    runConfig.runPriority = 21U;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext,
        (RunKernelTraceLength == 1U) && (RunKernelTrace[0] == 'H'));

    RunKernelTraceLength = 0U;
    runConfig.runName = "round-a";
    runConfig.runEntry = run_kernel_round_worker;
    runConfig.runArgument = &runFirstId;
    runConfig.runPriority = 5U;
    runConfig.runTimeSliceTicks = 1U;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    runConfig.runName = "round-b";
    runConfig.runArgument = &runSecondId;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunKernelDone, 100U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunKernelDone, 100U), RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext,
        (RunKernelTraceLength == 4U) && (RunKernelTrace[0] == 'A') &&
        (RunKernelTrace[1] == 'B') && (RunKernelTrace[2] == 'A') &&
        (RunKernelTrace[3] == 'B'));

    RunKernelTraceLength = 0U;
    runConfig.runName = "slice-a";
    runConfig.runEntry = run_kernel_slice_worker;
    runConfig.runArgument = &runFirstId;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    runConfig.runName = "slice-b";
    runConfig.runArgument = &runSecondId;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunKernelDone, 100U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunKernelDone, 100U), RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext,
        (RunKernelTraceLength == 8U) && (RunKernelTrace[0] == 'A') &&
        (RunKernelTrace[1] == 'B') && (RunKernelTrace[6] == 'A'));
    if (!((RunKernelTraceLength == 8U) && (RunKernelTrace[0] == 'A') &&
          (RunKernelTrace[1] == 'B') && (RunKernelTrace[6] == 'A')))
    {
        RUN_CHAR runTraceText[64];
        RUN_SIZE runTraceLength = 0U;
        if (RunKernelTraceLength < sizeof(RunKernelTrace))
        {
            RunKernelTrace[RunKernelTraceLength] = '\0';
        }
        (RUN_VOID)run_format_buffer(
            runTraceText, sizeof(runTraceText), &runTraceLength,
            "slice trace length=%u values=%s\r\n",
            RunKernelTraceLength, RunKernelTrace);
        run_test_output(runTraceText);
    }

    runBefore = RunKernel.runTicks;
    RUN_TEST_STATUS(runContext, run_thread_sleep(3U), RUN_STATUS_TIMEOUT);
    RUN_TEST_CHECK(runContext, RunKernel.runTicks >= runBefore + 3U);

    RunKernel.runTicks = 0xFFFFFFF0U;
    runBefore = RunKernel.runTicks;
    RUN_TEST_STATUS(runContext, run_thread_sleep(3U), RUN_STATUS_TIMEOUT);
    RUN_TEST_CHECK(runContext, RunKernel.runTicks == 0xFFFFFFF3U);
    RUN_TEST_CHECK(runContext, RunKernel.runTicks > runBefore);

    runConfig.runName = "resume";
    runConfig.runEntry = run_kernel_resume_worker;
    runConfig.runArgument = &runCounter;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_thread_suspend(runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_thread_resume(runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunKernelDone, 100U), RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext, runCounter == 1U);

    runConfig.runName = "delete-ready";
    runConfig.runEntry = run_kernel_noop_worker;
    runConfig.runArgument = RUN_NULL;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_thread_delete(runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_thread_get_stats(runThread, &(RunThreadStats){0}),
                    RUN_STATUS_INVALID_ARGUMENT);

    RUN_TEST_STATUS(runContext,
                    run_semaphore_create(0U, 1U, &runForever), RUN_STATUS_OK);
    runConfig.runName = "delete-blocked";
    runConfig.runEntry = run_kernel_forever_worker;
    runConfig.runArgument = runForever;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_thread_sleep(1U), RUN_STATUS_TIMEOUT);
    RUN_TEST_STATUS(runContext, run_thread_delete(runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_semaphore_delete(runForever), RUN_STATUS_OK);

    RUN_TEST_STATUS(runContext, run_thread_get_info(0U, &runInfo), RUN_STATUS_OK);
    run_kernel_get_stats(&runKernelStats);
    RUN_TEST_CHECK(runContext, runKernelStats.runThreadCount >= 3U);
    RUN_TEST_CHECK(runContext,
        runInfo.runStats.runStackHighWater <= runInfo.runStats.runStackSize);
    runStatus = run_thread_get_info(999U, &runInfo);
    RUN_TEST_STATUS(runContext, runStatus, RUN_STATUS_INVALID_ARGUMENT);
    RUN_TEST_STATUS(runContext, run_semaphore_delete(RunKernelDone),
                    RUN_STATUS_OK);
}
