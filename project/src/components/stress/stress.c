/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - background stress worker
 *
 * Layer: components/stress
 * Purpose: Repeatedly exercise allocator, queue, semaphore, mutex, event, timer.
 * Dependencies: public scheduler, synchronization, and memory services.
 * Constraint: The worker yields every iteration and leaves resources allocated
 *             until deletion so allocation and cleanup errors remain measurable.
 */
#include "runrtos/components/stress/run_stress.h"
#include "runrtos/core/run_config.h"

typedef RUN_STRUCT RunStressState
{
    RunMemory *runMemory;
    RunThread *runThread;
    RunQueue *runQueue;
    RunSemaphore *runSemaphore;
    RunMutex *runMutex;
    RunEventGroup *runEvents;
    RunTimer *runTimer;
    RUN_UINT_32 runDeadlineTick;
    RUN_UINT_32 runIterations;
    RUN_UINT_32 runFailures;
    RUN_SIZE runMemoryBaseline;
    RUN_SIZE runMemoryUsed;
    RUN_SIZE runMemoryPeak;
    RUN_UINT_32 runAllocatorFailures;
    RUN_SIZE runStackSize;
    RUN_SIZE runStackHighWater;
    RunStressConfig runConfig;
    RUN_VOLATILE RUN_BOOL runActive;
    RUN_VOLATILE RUN_BOOL runStopRequested;
} RunStressState;

/* Completes the timer leg of each allocator and kernel stress iteration. */
RUN_STATIC RUN_VOID run_stress_timer_callback(RunTimer *runTimer,
                                              RUN_VOID *runArgument)
{
    (RUN_VOID)runTimer;
    (RUN_VOID)runArgument;
}

/* Counts a non-OK status while preserving details for diagnostics. */
RUN_STATIC RUN_VOID run_stress_fail(RunStressState *runState,
                                    RunStatus runStatus)
{
    if (runStatus != RUN_STATUS_OK)
    {
        runState->runFailures++;
    }
}

/* Refreshes current and peak allocator usage after a stress iteration. */
RUN_STATIC RUN_VOID run_stress_capture_memory(RunStressState *runState)
{
    RunMemoryStats runStats;
    run_memory_get_stats(runState->runMemory, &runStats);
    runState->runMemoryUsed = runStats.runUsed;
    runState->runMemoryPeak = runStats.runPeakUsed;
    runState->runAllocatorFailures = runStats.runFailureCount;
}

/* Exercises allocator, IPC, event, and timer paths in a bounded loop. */
RUN_STATIC RUN_VOID run_stress_worker(RUN_VOID *runArgument)
{
    RunStressState *runState = (RunStressState *)runArgument;
    RUN_UINT_32 runQueueValue = 0U;
    RUN_UINT_32 runObserved = 0U;
    RunThreadStats runStackStats;

    for (;;)
    {
        RunKernelStats runKernelStats;
        RunStatus runStatus;
        RUN_SIZE runAllocationSize = 16U + (RUN_SIZE)(runState->runIterations & 63U);

        run_kernel_get_stats(&runKernelStats);
        if (runState->runStopRequested ||
            ((RUN_UINT_32)runKernelStats.runTicks - runState->runDeadlineTick <
             0x80000000U))
        {
            break;
        }
        {
            RUN_VOID *runAllocation = RUN_NULL;
            runStatus = run_memory_allocate(runState->runMemory,
                                            runAllocationSize,
                                            RUN_MEMORY_ALIGNMENT,
                                            &runAllocation);
            run_stress_fail(runState, runStatus);
            if (runAllocation != RUN_NULL)
            {
                run_memory_set(runAllocation, (RUN_INT_32)(runState->runIterations & 0x7FU),
                               runAllocationSize);
                run_stress_fail(runState,
                                run_memory_release(runState->runMemory,
                                                   runAllocation));
            }
        }
        runQueueValue = (RUN_UINT_32)(runState->runIterations ^ 0xA5A5U);
        run_stress_fail(runState, run_queue_send(runState->runQueue,
                                                 &runQueueValue,
                                                 RUN_TIME_NO_WAIT));
        run_stress_fail(runState, run_queue_receive(runState->runQueue,
                                                    &runObserved,
                                                    RUN_TIME_NO_WAIT));
        run_stress_fail(runState,
                        run_semaphore_give(runState->runSemaphore));
        run_stress_fail(runState,
                        run_semaphore_take(runState->runSemaphore,
                                           RUN_TIME_NO_WAIT));
        run_stress_fail(runState, run_mutex_lock(runState->runMutex,
                                                 RUN_TIME_NO_WAIT));
        run_stress_fail(runState, run_mutex_unlock(runState->runMutex));
        run_stress_fail(runState,
                        run_event_group_set(runState->runEvents,
                                            runState->runIterations | 1U));
        run_stress_fail(runState,
                        run_event_group_wait(runState->runEvents, 1U,
                                             RUN_FALSE, RUN_TRUE,
                                             &runObserved,
                                             RUN_TIME_NO_WAIT));
        run_stress_fail(runState, run_timer_start(runState->runTimer, 1U));
        run_stress_fail(runState, run_timer_stop(runState->runTimer));
        runState->runIterations++;
        (RUN_VOID)run_thread_sleep(1U);
    }
    (RUN_VOID)run_thread_get_stats(run_thread_current(), &runStackStats);
    runState->runStackSize = runStackStats.runStackSize;
    runState->runStackHighWater = runStackStats.runStackHighWater;
    run_stress_capture_memory(runState);
    runState->runActive = RUN_FALSE;
    if (runState->runConfig.runComplete != RUN_NULL)
    {
        RunStressInfo runInfo;
        run_stress_get_info((RunStress *)runState, &runInfo);
        runState->runConfig.runComplete((RunStress *)runState, &runInfo,
                                        runState->runConfig.runCompleteArgument);
    }
}

/* Allocates a reusable stress harness and its required kernel objects. */
RunStatus run_stress_create(RUN_CONST RunStressConfig *runConfig,
                            RunStress **runStressOut)
{
    RunStressState *runState;
    RunMutexConfig runMutexConfig = { RUN_FALSE };
    RunMemoryStats runMemoryStats;
    RunStatus runStatus;
    if ((runConfig == RUN_NULL) || (runStressOut == RUN_NULL) ||
        (runConfig->runMemory == RUN_NULL) ||
        (runConfig->runStackSize < 512U))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    *runStressOut = RUN_NULL;
    runStatus = run_memory_allocate(runConfig->runMemory, sizeof(RunStressState),
                                    RUN_MEMORY_ALIGNMENT,
                                    (RUN_VOID **)&runState);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    run_memory_set(runState, 0, sizeof(RunStressState));
    runState->runMemory = runConfig->runMemory;
    runState->runConfig = *runConfig;
    runStatus = run_queue_create(sizeof(RUN_UINT_32), 4U, &runState->runQueue);
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_semaphore_create(0U, 1U, &runState->runSemaphore);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_mutex_create(&runMutexConfig, &runState->runMutex);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_event_group_create(&runState->runEvents);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_timer_create(run_stress_timer_callback, runState,
                                     RUN_FALSE, 0U, &runState->runTimer);
    }
    if (runStatus != RUN_STATUS_OK)
    {
        (RUN_VOID)run_stress_delete((RunStress *)runState);
        return runStatus;
    }
    run_memory_get_stats(runState->runMemory, &runMemoryStats);
    runState->runMemoryBaseline = runMemoryStats.runUsed;
    *runStressOut = (RunStress *)runState;
    return RUN_STATUS_OK;
}

/* Starts a fresh background worker for the requested duration in seconds. */
RunStatus run_stress_start(RunStress *runStress, RUN_UINT_32 runSeconds)
{
    RunStressState *runState = (RunStressState *)runStress;
    RunThreadConfig runThreadConfig;
    RunKernelStats runKernelStats;
    RunMemoryStats runMemoryStats;
    RunStatus runStatus;
    if ((runState == RUN_NULL) || run_kernel_is_in_isr())
    {
        return (runState == RUN_NULL) ?
            RUN_STATUS_INVALID_ARGUMENT : RUN_STATUS_CONTEXT;
    }
    if (runState->runActive)
    {
        return RUN_STATUS_BUSY;
    }
    if (runState->runThread != RUN_NULL)
    {
        (RUN_VOID)run_thread_delete(runState->runThread);
        runState->runThread = RUN_NULL;
    }
    if (runSeconds == 0U)
    {
        runSeconds = 60U;
    }
    run_kernel_get_stats(&runKernelStats);
    run_memory_get_stats(runState->runMemory, &runMemoryStats);
    runState->runMemoryBaseline = runMemoryStats.runUsed;
    runState->runIterations = 0U;
    runState->runFailures = 0U;
    runState->runStopRequested = RUN_FALSE;
    runState->runDeadlineTick = (RUN_UINT_32)runKernelStats.runTicks +
        (runSeconds * RUN_CONFIG_TICK_HZ);
    runState->runActive = RUN_TRUE;
    runThreadConfig.runName = "stress";
    runThreadConfig.runEntry = run_stress_worker;
    runThreadConfig.runArgument = runState;
    runThreadConfig.runStackSize = runState->runConfig.runStackSize;
    runThreadConfig.runPriority = runState->runConfig.runPriority;
    runThreadConfig.runTimeSliceTicks = 1U;
    runStatus = run_thread_create(&runThreadConfig, &runState->runThread);
    if (runStatus != RUN_STATUS_OK)
    {
        runState->runActive = RUN_FALSE;
    }
    return runStatus;
}

/* Requests the active stress worker to stop after its current iteration. */
RunStatus run_stress_stop(RunStress *runStress)
{
    RunStressState *runState = (RunStressState *)runStress;
    if ((runState == RUN_NULL) || run_kernel_is_in_isr())
    {
        return (runState == RUN_NULL) ?
            RUN_STATUS_INVALID_ARGUMENT : RUN_STATUS_CONTEXT;
    }
    if (!runState->runActive)
    {
        return RUN_STATUS_INVALID_STATE;
    }
    runState->runStopRequested = RUN_TRUE;
    return RUN_STATUS_OK;
}

/* Copies a consistent stress-result snapshot for shell and test consumers. */
RUN_VOID run_stress_get_info(RunStress *runStress, RunStressInfo *runInfoOut)
{
    RunStressState *runState = (RunStressState *)runStress;
    if ((runState == RUN_NULL) || (runInfoOut == RUN_NULL))
    {
        return;
    }
    runInfoOut->runActive = runState->runActive;
    runInfoOut->runIterations = runState->runIterations;
    runInfoOut->runFailures = runState->runFailures;
    runInfoOut->runMemoryBaseline = runState->runMemoryBaseline;
    runInfoOut->runMemoryUsed = runState->runMemoryUsed;
    runInfoOut->runMemoryPeak = runState->runMemoryPeak;
    runInfoOut->runAllocatorFailures = runState->runAllocatorFailures;
    runInfoOut->runStackSize = runState->runStackSize;
    runInfoOut->runStackHighWater = runState->runStackHighWater;
}

/* Stops the worker, destroys owned objects, and releases the stress harness. */
RunStatus run_stress_delete(RunStress *runStress)
{
    RunStressState *runState = (RunStressState *)runStress;
    if ((runState == RUN_NULL) || run_kernel_is_in_isr())
    {
        return (runState == RUN_NULL) ?
            RUN_STATUS_INVALID_ARGUMENT : RUN_STATUS_CONTEXT;
    }
    if (runState->runActive)
    {
        runState->runStopRequested = RUN_TRUE;
        for (RUN_UINT_32 runAttempt = 0U;
             (runAttempt < 2000U) && runState->runActive; runAttempt++)
        {
            (RUN_VOID)run_thread_sleep(1U);
        }
        if (runState->runActive)
        {
            return RUN_STATUS_BUSY;
        }
    }
    if (runState->runThread != RUN_NULL)
    {
        (RUN_VOID)run_thread_delete(runState->runThread);
    }
    if (runState->runTimer != RUN_NULL)
    {
        (RUN_VOID)run_timer_delete(runState->runTimer);
    }
    if (runState->runEvents != RUN_NULL)
    {
        (RUN_VOID)run_event_group_delete(runState->runEvents);
    }
    if (runState->runMutex != RUN_NULL)
    {
        (RUN_VOID)run_mutex_delete(runState->runMutex);
    }
    if (runState->runSemaphore != RUN_NULL)
    {
        (RUN_VOID)run_semaphore_delete(runState->runSemaphore);
    }
    if (runState->runQueue != RUN_NULL)
    {
        (RUN_VOID)run_queue_delete(runState->runQueue);
    }
    return run_memory_release(runState->runMemory, runState);
}
