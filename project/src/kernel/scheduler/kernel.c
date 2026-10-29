/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - deterministic scheduler and thread lifecycle
 *
 * Layer: kernel
 * Purpose: Maintain ready/timeout queues, context switching, and thread state.
 * Dependencies: private kernel state, runtime allocation, and arch port.
 * Constraint: All list/bitmap/state transitions run with interrupts disabled.
 *             Higher numeric priorities preempt lower priorities.
 */
#include "../internal/run_kernel_internal.h"

#define RUN_THREAD_STACK_FILL 0xA5U
#define RUN_THREAD_MIN_STACK_BYTES 128U

/* Kernel metadata is private; public headers expose only RunThread pointers. */
RunKernelState RunKernel;

/* PendSV assembly reads this symbol directly, so it is kept separate from state. */
RunThread *RUN_VOLATILE RunKernelCurrent;

RUN_STATIC RUN_VOID run_kernel_ready_insert(RunThread *runThread)
{
    RUN_UINT_32 runPriority = runThread->runPriority;

    /* FIFO order provides round-robin behavior within one priority. */
    runThread->runReadyNext = RUN_NULL;
    if (RunKernel.runReadyLast[runPriority] == RUN_NULL)
    {
        RunKernel.runReady[runPriority] = runThread;
        RunKernel.runReadyLast[runPriority] = runThread;
    }
    else
    {
        RunKernel.runReadyLast[runPriority]->runReadyNext = runThread;
        RunKernel.runReadyLast[runPriority] = runThread;
    }
    RunKernel.runReadyBitmap |= (1UL << runPriority);
}

RUN_STATIC RUN_VOID run_kernel_ready_remove(RunThread *runThread)
{
    RUN_UINT_32 runPriority = runThread->runPriority;
    RunThread *runCurrent = RunKernel.runReady[runPriority];
    RunThread *runPrevious = RUN_NULL;

    while (runCurrent != RUN_NULL)
    {
        if (runCurrent == runThread)
        {
            if (runPrevious == RUN_NULL)
            {
                RunKernel.runReady[runPriority] = runCurrent->runReadyNext;
            }
            else
            {
                runPrevious->runReadyNext = runCurrent->runReadyNext;
            }
            if (RunKernel.runReadyLast[runPriority] == runCurrent)
            {
                RunKernel.runReadyLast[runPriority] = runPrevious;
            }
            if (RunKernel.runReady[runPriority] == RUN_NULL)
            {
                RunKernel.runReadyBitmap &= ~(1UL << runPriority);
            }
            runThread->runReadyNext = RUN_NULL;
            return;
        }
        runPrevious = runCurrent;
        runCurrent = runCurrent->runReadyNext;
    }
}

RUN_STATIC RunThread *run_kernel_ready_pop_highest(RUN_VOID)
{
    RunThread *runThread;
    RUN_UINT_32 runPriority;
    if (RunKernel.runReadyBitmap == 0U)
    {
        return RUN_NULL;
    }
    /* The bitmap position maps directly to the highest ready priority. */
    runPriority = 31U - run_compiler_count_leading_zeros(RunKernel.runReadyBitmap);
    runThread = RunKernel.runReady[runPriority];
    if (runThread != RUN_NULL)
    {
        run_kernel_ready_remove(runThread);
    }
    return runThread;
}

RUN_STATIC RUN_VOID run_kernel_timeout_insert(RunThread *runThread)
{
    RunThread *runCurrent = RunKernel.runTimeoutFirst;
    RunThread *runPrevious = RUN_NULL;

    /*
     * Signed subtraction gives wraparound-safe ordering as long as inserted
     * deadlines stay within half of the 32-bit tick range.
     */
    while ((runCurrent != RUN_NULL) &&
           ((RUN_INT_32)(runCurrent->runTimeoutAt - runThread->runTimeoutAt) <= 0))
    {
        runPrevious = runCurrent;
        runCurrent = runCurrent->runTimeoutNext;
    }
    runThread->runTimeoutNext = runCurrent;
    if (runPrevious == RUN_NULL)
    {
        RunKernel.runTimeoutFirst = runThread;
    }
    else
    {
        runPrevious->runTimeoutNext = runThread;
    }
}

RUN_STATIC RUN_VOID run_kernel_timeout_remove(RunThread *runThread)
{
    RunThread *runCurrent = RunKernel.runTimeoutFirst;
    RunThread *runPrevious = RUN_NULL;
    while (runCurrent != RUN_NULL)
    {
        if (runCurrent == runThread)
        {
            if (runPrevious == RUN_NULL)
            {
                RunKernel.runTimeoutFirst = runCurrent->runTimeoutNext;
            }
            else
            {
                runPrevious->runTimeoutNext = runCurrent->runTimeoutNext;
            }
            runThread->runTimeoutNext = RUN_NULL;
            return;
        }
        runPrevious = runCurrent;
        runCurrent = runCurrent->runTimeoutNext;
    }
}

RUN_STATIC RUN_VOID run_kernel_update_switch_request(RUN_VOID)
{
    if ((RunKernel.runCurrent != RUN_NULL) &&
        (RunKernel.runReadyBitmap != 0U) &&
        ((RunKernel.runSchedulerLock == 0U)))
    {
        RUN_UINT_32 runHighest = 31U - run_compiler_count_leading_zeros(RunKernel.runReadyBitmap);
        if (runHighest > RunKernel.runCurrent->runPriority)
        {
            run_kernel_request_switch();
        }
    }
}

RUN_VOID run_kernel_wait_list_append(RunWaitList *runWaitList, RunThread *runThread)
{
    RUN_INVARIANT(runWaitList != RUN_NULL);
    RUN_INVARIANT(runThread != RUN_NULL);
    runThread->runWaitNext = RUN_NULL;
    runThread->runWaitPrevious = runWaitList->runLast;
    runThread->runWaitList = runWaitList;
    if (runWaitList->runLast == RUN_NULL)
    {
        runWaitList->runFirst = runThread;
    }
    else
    {
        runWaitList->runLast->runWaitNext = runThread;
    }
    runWaitList->runLast = runThread;
}

RUN_VOID run_kernel_wait_list_remove(RunThread *runThread)
{
    RunWaitList *runWaitList;
    if ((runThread == RUN_NULL) || (runThread->runWaitList == RUN_NULL))
    {
        return;
    }
    runWaitList = runThread->runWaitList;
    if (runThread->runWaitPrevious == RUN_NULL)
    {
        runWaitList->runFirst = runThread->runWaitNext;
    }
    else
    {
        runThread->runWaitPrevious->runWaitNext = runThread->runWaitNext;
    }
    if (runThread->runWaitNext == RUN_NULL)
    {
        runWaitList->runLast = runThread->runWaitPrevious;
    }
    else
    {
        runThread->runWaitNext->runWaitPrevious = runThread->runWaitPrevious;
    }
    runThread->runWaitNext = RUN_NULL;
    runThread->runWaitPrevious = RUN_NULL;
    runThread->runWaitList = RUN_NULL;
}

RunThread *run_kernel_wait_list_pop(RunWaitList *runWaitList)
{
    RunThread *runThread;
    if ((runWaitList == RUN_NULL) || (runWaitList->runFirst == RUN_NULL))
    {
        return RUN_NULL;
    }
    runThread = runWaitList->runFirst;
    run_kernel_wait_list_remove(runThread);
    return runThread;
}

RUN_VOID run_kernel_ready_thread(RunThread *runThread, RunStatus runWakeStatus)
{
    if ((runThread == RUN_NULL) || (runThread->runState == RUN_THREAD_TERMINATED))
    {
        return;
    }
    if (runThread->runState == RUN_THREAD_BLOCKED)
    {
        /* Exactly one waiter/timeout node is linked while a thread is blocked. */
        run_kernel_wait_list_remove(runThread);
        run_kernel_timeout_remove(runThread);
    }
    if ((runThread->runState == RUN_THREAD_READY) ||
        (runThread->runState == RUN_THREAD_RUNNING))
    {
        runThread->runWakeStatus = (RUN_UINT_32)runWakeStatus;
        return;
    }
    runThread->runWakeStatus = (RUN_UINT_32)runWakeStatus;
    runThread->runState = RUN_THREAD_READY;
    run_kernel_ready_insert(runThread);
    run_kernel_update_switch_request();
}

RUN_VOID run_kernel_thread_set_priority(RunThread *runThread,
                                        RUN_UINT_8 runPriority)
{
    RUN_INVARIANT(runThread != RUN_NULL);
    RUN_INVARIANT(runPriority < RUN_CONFIG_PRIORITY_COUNT);
    if (runThread->runPriority == runPriority)
    {
        return;
    }
    /*
     * A ready thread is indexed by its current priority. Moving it requires
     * unlinking from the old bucket before changing the index, then inserting
     * into the new FIFO tail so priority inheritance cannot corrupt the bitmap.
     */
    if (runThread->runState == RUN_THREAD_READY)
    {
        run_kernel_ready_remove(runThread);
        runThread->runPriority = runPriority;
        run_kernel_ready_insert(runThread);
    }
    else
    {
        runThread->runPriority = runPriority;
    }
    run_kernel_update_switch_request();
}

RunStatus run_kernel_block_current(RunTimeMs runTimeout,
                                   RunWaitList *runWaitList,
                                   RUN_UINT_32 *runWakeStatusOut)
{
    RunThread *runThread = RunKernel.runCurrent;
    RunCriticalState runState;
    if ((runThread == RUN_NULL) || (runThread == RunKernel.runIdle))
    {
        return RUN_STATUS_CONTEXT;
    }
    if (run_kernel_is_in_isr())
    {
        return RUN_STATUS_CONTEXT;
    }

    /*
     * Resource checks in callers and these transitions share this critical
     * section, preventing a give/send between "not available" and block.
     */
    runState = run_arch_enter_critical();
    runThread->runWakeStatus = (RUN_UINT_32)RUN_STATUS_TIMEOUT;
    runThread->runState = RUN_THREAD_BLOCKED;
    if (runWaitList != RUN_NULL)
    {
        run_kernel_wait_list_append(runWaitList, runThread);
    }
    if (runTimeout != RUN_TIME_FOREVER)
    {
        /* Keep deadlines inside the signed half-range used by wrap comparisons. */
        if (runTimeout > ((RunTimeMs)0x7FFFFFFFU * 1000U / RUN_CONFIG_TICK_HZ))
        {
            runTimeout = (RunTimeMs)0x7FFFFFFFU * 1000U / RUN_CONFIG_TICK_HZ;
        }
        runThread->runTimeoutAt = (RunTick)RunKernel.runTicks +
            (RunTick)((runTimeout * RUN_CONFIG_TICK_HZ + 999U) / 1000U);
        /* A positive wait must not complete in its starting tick. */
        if (runThread->runTimeoutAt == (RunTick)RunKernel.runTicks)
        {
            runThread->runTimeoutAt++;
        }
        run_kernel_timeout_insert(runThread);
    }
    run_kernel_request_switch();
    run_arch_exit_critical(runState);
    if (runWakeStatusOut != RUN_NULL)
    {
        *runWakeStatusOut = runThread->runWakeStatus;
    }
    return (RunStatus)runThread->runWakeStatus;
}

RunStatus run_kernel_api_enter(RUN_BOOL runAllowIsr)
{
    /* Dedicated *_from_isr paths bypass this guard and validate their own state. */
    if (!RunKernel.runInitialized)
    {
        return RUN_STATUS_NOT_INITIALIZED;
    }
    if (!runAllowIsr && run_kernel_is_in_isr())
    {
        return RUN_STATUS_CONTEXT;
    }
    return RUN_STATUS_OK;
}

RUN_STATIC RUN_VOID run_thread_trampoline(RunThread *runThread)
{
    /* Returning from user entry follows the same path as an explicit exit. */
    runThread->runEntry(runThread->runArgument);
    run_thread_exit();
}

RUN_STATIC RUN_VOID run_thread_exit_stub(RUN_VOID)
{
    run_thread_exit();
}

RunStatus run_kernel_create_internal(RUN_CONST RunThreadConfig *runConfig,
                                     RunThread **runThreadOut)
{
    RunThread *runThread;
    RUN_VOID *runObject;
    RUN_VOID *runStack;
    RunStatus runStatus;
    RunArchThreadContextConfig runContextConfig;
    RUN_SIZE runContext;

    if ((runConfig == RUN_NULL) || (runThreadOut == RUN_NULL) ||
        (runConfig->runEntry == RUN_NULL) ||
        (runConfig->runPriority >= RUN_CONFIG_PRIORITY_COUNT) ||
        (runConfig->runStackSize < RUN_THREAD_MIN_STACK_BYTES))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }

    /* Object and stack have independent ownership and must free as a pair. */
    runStatus = run_memory_allocate(RunKernel.runMemory, sizeof(RunThread),
                                    RUN_MEMORY_ALIGNMENT, &runObject);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runStatus = run_memory_allocate(RunKernel.runMemory, runConfig->runStackSize,
                                    RUN_MEMORY_ALIGNMENT, &runStack);
    if (runStatus != RUN_STATUS_OK)
    {
        (RUN_VOID)run_memory_release(RunKernel.runMemory, runObject);
        return runStatus;
    }

    runThread = (RunThread *)runObject;
    run_memory_set(runThread, 0, sizeof(RunThread));
    runThread->runMemory = RunKernel.runMemory;
    runThread->runStackBase = (RUN_UINT_8 *)runStack;
    runThread->runStackSize = runConfig->runStackSize;
    runThread->runEntry = runConfig->runEntry;
    runThread->runArgument = runConfig->runArgument;
    runThread->runName = (runConfig->runName != RUN_NULL) ? runConfig->runName : "thread";
    runThread->runBasePriority = runConfig->runPriority;
    runThread->runPriority = runConfig->runPriority;
    /* Zero is a convenient default without making the public config uninitialized. */
    runThread->runTimeSlice = (runConfig->runTimeSliceTicks == 0U) ?
        RUN_CONFIG_TICK_HZ : runConfig->runTimeSliceTicks;
    runThread->runTimeSliceRemaining = runThread->runTimeSlice;
    runThread->runState = RUN_THREAD_READY;
    run_memory_set(runStack, RUN_THREAD_STACK_FILL, runConfig->runStackSize);
    runContextConfig.runStackBase = runStack;
    runContextConfig.runStackSize = runConfig->runStackSize;
    runContextConfig.runEntry = (RUN_VOID (*)(RUN_VOID *))run_thread_trampoline;
    runContextConfig.runEntryArgument = runThread;
    runContextConfig.runReturn = run_thread_exit_stub;
    runStatus = run_arch_context_initialize(&runContextConfig, &runContext);
    if (runStatus != RUN_STATUS_OK)
    {
        (RUN_VOID)run_memory_release(RunKernel.runMemory, runStack);
        (RUN_VOID)run_memory_release(RunKernel.runMemory, runObject);
        return runStatus;
    }
    runThread->runContext = runContext;
    runThread->runWakeStatus = (RUN_UINT_32)RUN_STATUS_OK;

    runThread->runAllNext = RunKernel.runThreads;
    RunKernel.runThreads = runThread;
    RunKernel.runThreadCount++;
    run_kernel_ready_insert(runThread);
    run_kernel_update_switch_request();
    *runThreadOut = runThread;
    return RUN_STATUS_OK;
}

RunStatus run_thread_create(RUN_CONST RunThreadConfig *runConfig, RunThread **runThreadOut)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runState = run_arch_enter_critical();
    runStatus = run_kernel_create_internal(runConfig, runThreadOut);
    run_arch_exit_critical(runState);
    return runStatus;
}

RUN_BOOL run_kernel_validate_thread(RunThread *runThread)
{
    /* Linear lookup also rejects stale/freed pointers before every public action. */
    RunThread *runCurrent = RunKernel.runThreads;
    while (runCurrent != RUN_NULL)
    {
        if (runCurrent == runThread)
        {
            return RUN_TRUE;
        }
        runCurrent = runCurrent->runAllNext;
    }
    return RUN_FALSE;
}

RUN_STATIC RUN_VOID run_kernel_remove_all_list(RunThread *runThread)
{
    RunThread *runCurrent = RunKernel.runThreads;
    RunThread *runPrevious = RUN_NULL;
    while (runCurrent != RUN_NULL)
    {
        if (runCurrent == runThread)
        {
            if (runPrevious == RUN_NULL)
            {
                RunKernel.runThreads = runCurrent->runAllNext;
            }
            else
            {
                runPrevious->runAllNext = runCurrent->runAllNext;
            }
            RunKernel.runThreadCount--;
            return;
        }
        runPrevious = runCurrent;
        runCurrent = runCurrent->runAllNext;
    }
}

RUN_STATIC RUN_VOID run_thread_free(RunThread *runThread)
{
    RunMemory *runMemory = runThread->runMemory;
    RUN_UINT_8 *runStack = runThread->runStackBase;
    run_kernel_remove_all_list(runThread);
    run_arch_context_destroy(runThread->runContext);
    (RUN_VOID)run_memory_release(runMemory, runThread);
    if (runStack != RUN_NULL)
    {
        (RUN_VOID)run_memory_release(runMemory, runStack);
    }
}

RunStatus run_thread_delete(RunThread *runThread)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runState = run_arch_enter_critical();
    if (!run_kernel_validate_thread(runThread) || (runThread == RunKernel.runIdle))
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    if (runThread->runState == RUN_THREAD_READY)
    {
        run_kernel_ready_remove(runThread);
    }
    if (runThread->runState == RUN_THREAD_BLOCKED)
    {
        run_kernel_wait_list_remove(runThread);
        run_kernel_timeout_remove(runThread);
    }
    runThread->runState = RUN_THREAD_TERMINATED;
    if (runThread == RunKernel.runCurrent)
    {
        run_kernel_request_switch();
    }
    else
    {
        run_thread_free(runThread);
    }
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

RunThread *run_thread_current(RUN_VOID)
{
    return RunKernelCurrent;
}

RUN_VOID run_kernel_switch_prepare(RUN_VOID)
{
    RunThread *runOld = RunKernel.runCurrent;
    RunThread *runNext;

    if (RunKernel.runReap != RUN_NULL)
    {
        RunThread *runReap = RunKernel.runReap;
        RunKernel.runReap = RUN_NULL;
        run_thread_free(runReap);
    }
    RunKernel.runSwitchRequested = RUN_FALSE;
    if (runOld != RUN_NULL)
    {
        runOld->runContextSwitches++;
    }
    /* A blocked, suspended, or terminated outgoing thread is not re-queued. */
    if ((runOld != RUN_NULL) && (runOld->runState == RUN_THREAD_RUNNING))
    {
        runOld->runState = RUN_THREAD_READY;
        run_kernel_ready_insert(runOld);
    }

    runNext = run_kernel_ready_pop_highest();
    if ((runNext == RUN_NULL) || (runNext == RunKernel.runIdle))
    {
        runNext = RunKernel.runIdle;
    }
    if (runNext != RUN_NULL)
    {
        runNext->runState = RUN_THREAD_RUNNING;
        runNext->runTimeSliceRemaining = runNext->runTimeSlice;
        RunKernel.runCurrent = runNext;
        RunKernelCurrent = runNext;
        RunKernel.runContextSwitches++;
    }

    /* The old stack is no longer referenced after PendSV restores runNext. */
    if ((runOld != RUN_NULL) && (runOld != runNext) &&
        (runOld->runState == RUN_THREAD_TERMINATED))
    {
        if (run_arch_context_requires_deferred_release())
        {
            RunKernel.runReap = runOld;
        }
        else
        {
            run_thread_free(runOld);
        }
    }
}

RUN_VOID run_kernel_request_switch(RUN_VOID)
{
    /* Locking defers PendSV until run_scheduler_unlock observes count zero. */
    if (RunKernel.runSchedulerLock == 0U)
    {
        RunKernel.runSwitchRequested = RUN_TRUE;
        run_arch_trigger_context_switch();
    }
}

RUN_VOID run_scheduler_yield(RUN_VOID)
{
    RunCriticalState runState = run_arch_enter_critical();
    if ((RunKernel.runCurrent != RUN_NULL) &&
        (RunKernel.runCurrent != RunKernel.runIdle))
    {
        run_kernel_request_switch();
    }
    run_arch_exit_critical(runState);
}

RunStatus run_thread_sleep(RunTimeMs runDuration)
{
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    if (runDuration == 0U)
    {
        run_scheduler_yield();
        return RUN_STATUS_OK;
    }
    return run_kernel_block_current(runDuration, RUN_NULL, RUN_NULL);
}

RunStatus run_thread_suspend(RunThread *runThread)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runState = run_arch_enter_critical();
    if (!run_kernel_validate_thread(runThread) || (runThread == RunKernel.runIdle) ||
        (runThread->runState == RUN_THREAD_TERMINATED))
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    if (runThread->runState == RUN_THREAD_READY)
    {
        run_kernel_ready_remove(runThread);
    }
    if (runThread->runState == RUN_THREAD_BLOCKED)
    {
        run_kernel_wait_list_remove(runThread);
        run_kernel_timeout_remove(runThread);
    }
    runThread->runState = RUN_THREAD_SUSPENDED;
    if (runThread == RunKernel.runCurrent)
    {
        run_kernel_request_switch();
    }
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

RunStatus run_thread_resume(RunThread *runThread)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runState = run_arch_enter_critical();
    if (!run_kernel_validate_thread(runThread) ||
        (runThread->runState != RUN_THREAD_SUSPENDED))
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_INVALID_STATE;
    }
    run_kernel_ready_thread(runThread, RUN_STATUS_OK);
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

RUN_VOID run_thread_exit(RUN_VOID)
{
    RunCriticalState runState = run_arch_enter_critical();
    if ((RunKernel.runCurrent != RUN_NULL) &&
        (RunKernel.runCurrent != RunKernel.runIdle))
    {
        RunKernel.runCurrent->runState = RUN_THREAD_TERMINATED;
        run_kernel_request_switch();
    }
    run_arch_exit_critical(runState);
    /* Stay on a safe loop if a fault delivery path defers the requested switch. */
    for (;;)
    {
        run_arch_idle();
    }
}

RUN_VOID run_kernel_idle_entry(RUN_VOID *runArgument)
{
    (RUN_VOID)runArgument;
    for (;;)
    {
        run_arch_idle();
    }
}

RUN_VOID run_scheduler_start(RUN_VOID)
{
    RunCriticalState runState;
    RunThread *runFirst;
    RunThreadConfig runIdleConfig = {
        "idle", run_kernel_idle_entry, RUN_NULL, 256U, 0U, 1U
    };
    RunStatus runStatus;

    if (!RunKernel.runInitialized)
    {
        run_panic("scheduler not initialized", __FILE__, (RUN_UINT_32)__LINE__,
                  RUN_STATUS_NOT_INITIALIZED);
    }
    /*
     * The idle thread is created only at start time because it needs the same
     * allocator-backed stack as application threads but must never be preempted
     * by the initial context switch setup.
     */
    runStatus = run_kernel_create_internal(&runIdleConfig, &RunKernel.runIdle);
    if (runStatus != RUN_STATUS_OK)
    {
        run_panic("idle allocation failed", __FILE__, (RUN_UINT_32)__LINE__, runStatus);
    }
    run_kernel_ready_remove(RunKernel.runIdle);
    RunKernel.runIdle->runState = RUN_THREAD_READY;

    runState = run_arch_enter_critical();
    runFirst = run_kernel_ready_pop_highest();
    if ((runFirst == RUN_NULL) || (runFirst == RunKernel.runIdle))
    {
        runFirst = RunKernel.runIdle;
    }
    runFirst->runState = RUN_THREAD_RUNNING;
    RunKernel.runCurrent = runFirst;
    RunKernelCurrent = runFirst;
    RunKernel.runRunning = RUN_TRUE;
    run_arch_start_scheduler();
    run_arch_exit_critical(runState);

    for (;;)
    {
        run_arch_idle();
    }
}

RUN_VOID run_kernel_tick_from_isr(RUN_VOID)
{
    RunThread *runTimeout;
    RunThread *runCurrent;

    if (!RunKernel.runInitialized)
    {
        return;
    }
    RunKernel.runTicks++;
    runTimeout = RunKernel.runTimeoutFirst;
    /* Signed difference handles tick wraparound without a special case. */
    while ((runTimeout != RUN_NULL) &&
           ((RUN_INT_32)((RunTick)RunKernel.runTicks - runTimeout->runTimeoutAt) >= 0))
    {
        RunKernel.runTimeoutFirst = runTimeout->runTimeoutNext;
        runTimeout->runTimeoutNext = RUN_NULL;
        /* The timeout list has already been unlinked; remove only the wait queue. */
        if (runTimeout->runState == RUN_THREAD_BLOCKED)
        {
            run_kernel_wait_list_remove(runTimeout);
            run_kernel_ready_thread(runTimeout, RUN_STATUS_TIMEOUT);
        }
        runTimeout = RunKernel.runTimeoutFirst;
    }

    runCurrent = RunKernel.runCurrent;
    if ((runCurrent != RUN_NULL) && (runCurrent != RunKernel.runIdle) &&
        (runCurrent->runState == RUN_THREAD_RUNNING) &&
        (runCurrent->runTimeSliceRemaining > 0U))
    {
        runCurrent->runTimeSliceRemaining--;
        if (runCurrent->runTimeSliceRemaining == 0U)
        {
            runCurrent->runTimeSliceRemaining = runCurrent->runTimeSlice;
            run_kernel_request_switch();
        }
    }
}

RUN_BOOL run_kernel_is_in_isr(RUN_VOID)
{
    return (run_arch_get_ipsr() != 0U) ? RUN_TRUE : RUN_FALSE;
}

RUN_VOID run_kernel_get_stats(RunKernelStats *runStats)
{
    if (runStats == RUN_NULL)
    {
        return;
    }
    runStats->runTicks = RunKernel.runTicks;
    runStats->runReadyCount = (RUN_UINT_32)__builtin_popcount(RunKernel.runReadyBitmap);
    runStats->runContextSwitches = RunKernel.runContextSwitches;
    runStats->runThreadCount = RunKernel.runThreadCount;
}

RunStatus run_thread_get_stats(RunThread *runThread, RunThreadStats *runStats)
{
    RUN_SIZE runUsed = 0U;
    if ((runThread == RUN_NULL) || (runStats == RUN_NULL) ||
        !run_kernel_validate_thread(runThread))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    /* The high-water mark is an approximation of the lowest untouched fill byte. */
    while ((runUsed < runThread->runStackSize) &&
           (runThread->runStackBase[runUsed] == RUN_THREAD_STACK_FILL))
    {
        runUsed++;
    }
    runStats->runName.runData = runThread->runName;
    runStats->runName.runLength = run_string_length(runThread->runName);
    runStats->runPriority = runThread->runPriority;
    runStats->runBasePriority = runThread->runBasePriority;
    runStats->runState = (RUN_UINT_32)runThread->runState;
    runStats->runStackSize = runThread->runStackSize;
    runStats->runStackHighWater = runThread->runStackSize - runUsed;
    runStats->runContextSwitches = runThread->runContextSwitches;
    return RUN_STATUS_OK;
}

RunStatus run_thread_get_info(RUN_SIZE runIndex, RunThreadInfo *runInfoOut)
{
    RunThread *runThread;
    RunStatus runStatus = RUN_STATUS_INVALID_ARGUMENT;
    RunCriticalState runState;

    if ((runInfoOut == RUN_NULL) ||
        (run_kernel_api_enter(RUN_FALSE) != RUN_STATUS_OK))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }

    runState = run_arch_enter_critical();
    runThread = RunKernel.runThreads;
    while ((runThread != RUN_NULL) && (runIndex > 0U))
    {
        runThread = runThread->runAllNext;
        runIndex--;
    }
    if (runThread != RUN_NULL)
    {
        runInfoOut->runThread = runThread;
        runStatus = run_thread_get_stats(runThread, &runInfoOut->runStats);
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

RUN_VOID run_scheduler_lock(RUN_VOID)
{
    RunCriticalState runState = run_arch_enter_critical();
    RunKernel.runSchedulerLock++;
    run_arch_exit_critical(runState);
}

RUN_VOID run_scheduler_unlock(RUN_VOID)
{
    RunCriticalState runState = run_arch_enter_critical();
    if (RunKernel.runSchedulerLock > 0U)
    {
        RunKernel.runSchedulerLock--;
    }
    /* Re-evaluate preemption only after the outermost lock is released. */
    if (RunKernel.runSchedulerLock == 0U)
    {
        run_kernel_update_switch_request();
    }
    run_arch_exit_critical(runState);
}

RunStatus run_kernel_init(RunMemory *runMemory)
{
    if (runMemory == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    run_memory_set(&RunKernel, 0, sizeof(RunKernel));
    RunKernel.runMemory = runMemory;
    RunKernel.runInitialized = RUN_TRUE;
    return RUN_STATUS_OK;
}

