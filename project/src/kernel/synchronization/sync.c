/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - synchronization, IPC, and software timers
 *
 * Layer: kernel
 * Purpose: Implement semaphores, mutexes, queues, events, and timer callbacks.
 * Dependencies: private kernel wait lists and runtime allocation.
 * Constraint: Blocking paths must re-check the resource after wakeup; ISR variants
 *             never block. Timer callbacks execute in the service thread.
 */
#include "../internal/run_kernel_internal.h"
#include "runrtos/kernel/run_sync.h"

#define RUN_SYNC_MAGIC_SEMAPHORE 0x52554E53U
#define RUN_SYNC_MAGIC_MUTEX 0x52554E4DU
#define RUN_SYNC_MAGIC_QUEUE 0x52554E51U
#define RUN_SYNC_MAGIC_EVENT 0x52554E45U
#define RUN_SYNC_MAGIC_TIMER 0x52554E54U

/* Magic values catch stale handles during debug; they do not provide ownership. */
RUN_STRUCT RunSemaphore
{
    RUN_UINT_32 runMagic;
    RUN_UINT_32 runCount;
    RUN_UINT_32 runMaximum;
    RunWaitList runWaiters;
};

RUN_STRUCT RunMutex
{
    RUN_UINT_32 runMagic;
    RunThread *runOwner;
    RUN_UINT_32 runRecursion;
    RUN_BOOL runRecursive;
    RunWaitList runWaiters;
    RunMutex *runNext;
};

RUN_STRUCT RunQueue
{
    RUN_UINT_32 runMagic;
    RUN_UINT_8 *runStorage;
    RUN_SIZE runItemSize;
    RUN_UINT_32 runCapacity;
    RUN_UINT_32 runHead;
    RUN_UINT_32 runCount;
    RunWaitList runSenders;
    RunWaitList runReceivers;
};

RUN_STRUCT RunEventGroup
{
    RUN_UINT_32 runMagic;
    RUN_UINT_32 runBits;
    RunWaitList runWaiters;
};

RUN_STRUCT RunTimer
{
    RUN_UINT_32 runMagic;
    RunTimerCallback runCallback;
    RUN_VOID *runArgument;
    RUN_BOOL runPeriodic;
    RUN_BOOL runRunning;
    RunTimeMs runPeriod;
    RunTick runDue;
    RunTimer *runNext;
};

RUN_STATIC RunTimer *RunTimerFirst;
RUN_STATIC RunThread *RunTimerThread;

RUN_STATIC RUN_VOID run_sync_destroy_waiters(RunWaitList *runWaitList, RunStatus runStatus)
{
    RunThread *runThread;
    while ((runThread = run_kernel_wait_list_pop(runWaitList)) != RUN_NULL)
    {
        /* Wake with a distinct status so callers do not confuse deletion with success. */
        run_kernel_ready_thread(runThread, runStatus);
    }
}

RUN_STATIC RUN_VOID run_timer_service_entry(RUN_VOID *runArgument)
{
    (RUN_VOID)runArgument;
    for (;;)
    {
        RunTimer *runTimer = RUN_NULL;
        RunCriticalState runState = run_arch_enter_critical();
        RunTick runNow = (RunTick)RunKernel.runTicks;
        /* Only list mutation is protected; user callbacks run with IRQs enabled. */
        if ((RunTimerFirst != RUN_NULL) &&
            ((RUN_INT_32)(runNow - RunTimerFirst->runDue) >= 0))
        {
            runTimer = RunTimerFirst;
            RunTimerFirst = runTimer->runNext;
            runTimer->runNext = RUN_NULL;
            /* Schedule from the current tick to avoid drift from callback latency. */
            if (runTimer->runPeriodic)
            {
                runTimer->runDue = runNow +
                    (RunTick)((runTimer->runPeriod * RUN_CONFIG_TICK_HZ + 999U) / 1000U);
            }
            else
            {
                runTimer->runRunning = RUN_FALSE;
            }
        }
        run_arch_exit_critical(runState);

        if (runTimer != RUN_NULL)
        {
            /*
             * The callback is outside the critical section so it may use ordinary
             * scheduler APIs. start() must not be called from this callback because
             * the service loop reinserts a still-running periodic timer below.
             */
            runTimer->runCallback(runTimer, runTimer->runArgument);
            if (runTimer->runPeriodic && runTimer->runRunning)
            {
                RunTimer *runCurrent;
                RunTimer *runPrevious = RUN_NULL;
                runState = run_arch_enter_critical();
                runCurrent = RunTimerFirst;
                while ((runCurrent != RUN_NULL) &&
                       ((RUN_INT_32)(runCurrent->runDue - runTimer->runDue) <= 0))
                {
                    runPrevious = runCurrent;
                    runCurrent = runCurrent->runNext;
                }
                runTimer->runNext = runCurrent;
                if (runPrevious == RUN_NULL)
                {
                    RunTimerFirst = runTimer;
                }
                else
                {
                    runPrevious->runNext = runTimer;
                }
                run_arch_exit_critical(runState);
            }
        }
        else
        {
            /* Sleep keeps the service thread blocked instead of polling forever. */
            (RUN_VOID)run_thread_sleep(1U);
        }
    }
}

RunStatus run_timer_system_init(RUN_VOID)
{
    RunThreadConfig runConfig = {
        "timer", run_timer_service_entry, RUN_NULL, 768U, 1U, 1U
    };
    return run_kernel_create_internal(&runConfig, &RunTimerThread);
}

RunStatus run_semaphore_create(RUN_UINT_32 runInitialCount,
                               RUN_UINT_32 runMaximumCount,
                               RunSemaphore **runSemaphoreOut)
{
    RunSemaphore *runSemaphore;
    RunStatus runStatus;
    RunCriticalState runState;
    if ((runSemaphoreOut == RUN_NULL) || (runMaximumCount == 0U) ||
        (runInitialCount > runMaximumCount))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runStatus = run_kernel_api_enter(RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    /* Allocation and publication are atomic so no half-created object is visible. */
    runState = run_arch_enter_critical();
    runStatus = run_memory_allocate(RunKernel.runMemory, sizeof(RunSemaphore),
                                    RUN_MEMORY_ALIGNMENT, (RUN_VOID **)&runSemaphore);
    if (runStatus == RUN_STATUS_OK)
    {
        run_memory_set(runSemaphore, 0, sizeof(RunSemaphore));
        runSemaphore->runMagic = RUN_SYNC_MAGIC_SEMAPHORE;
        runSemaphore->runCount = runInitialCount;
        runSemaphore->runMaximum = runMaximumCount;
        *runSemaphoreOut = runSemaphore;
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_semaphore_delete(RunSemaphore *runSemaphore)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runSemaphore == RUN_NULL) ||
        (runSemaphore->runMagic != RUN_SYNC_MAGIC_SEMAPHORE))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    /* Wake waiters before releasing storage; their stack still references this object. */
    run_sync_destroy_waiters(&runSemaphore->runWaiters, RUN_STATUS_CANCELLED);
    runSemaphore->runMagic = 0U;
    runStatus = run_memory_release(RunKernel.runMemory, runSemaphore);
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_semaphore_take(RunSemaphore *runSemaphore, RunTimeMs runTimeout)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runSemaphore == RUN_NULL) ||
        (runSemaphore->runMagic != RUN_SYNC_MAGIC_SEMAPHORE))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    if (runSemaphore->runCount > 0U)
    {
        runSemaphore->runCount--;
        run_arch_exit_critical(runState);
        return RUN_STATUS_OK;
    }
    if (runTimeout == RUN_TIME_NO_WAIT)
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_EMPTY;
    }
    run_arch_exit_critical(runState);
    return run_kernel_block_current(runTimeout, &runSemaphore->runWaiters, RUN_NULL);
}

RunStatus run_semaphore_take_from_isr(RunSemaphore *runSemaphore)
{
    RunCriticalState runState;
    RunStatus runStatus;
    if ((runSemaphore == RUN_NULL) || (runSemaphore->runMagic != RUN_SYNC_MAGIC_SEMAPHORE))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    runStatus = (runSemaphore->runCount > 0U) ? RUN_STATUS_OK : RUN_STATUS_EMPTY;
    if (runStatus == RUN_STATUS_OK)
    {
        runSemaphore->runCount--;
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_semaphore_give(RunSemaphore *runSemaphore)
{
    RunThread *runThread;
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runSemaphore == RUN_NULL) ||
        (runSemaphore->runMagic != RUN_SYNC_MAGIC_SEMAPHORE))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    /*
     * Direct handoff prevents another thread from stealing a newly posted unit
     * before the oldest waiter runs.
     */
    runThread = run_kernel_wait_list_pop(&runSemaphore->runWaiters);
    if (runThread != RUN_NULL)
    {
        run_kernel_ready_thread(runThread, RUN_STATUS_OK);
    }
    else if (runSemaphore->runCount < runSemaphore->runMaximum)
    {
        runSemaphore->runCount++;
    }
    else
    {
        runStatus = RUN_STATUS_FULL;
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_semaphore_give_from_isr(RunSemaphore *runSemaphore)
{
    RunThread *runThread;
    RunCriticalState runState;
    RunStatus runStatus = RUN_STATUS_OK;
    if ((runSemaphore == RUN_NULL) || (runSemaphore->runMagic != RUN_SYNC_MAGIC_SEMAPHORE))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    runThread = run_kernel_wait_list_pop(&runSemaphore->runWaiters);
    if (runThread != RUN_NULL)
    {
        run_kernel_ready_thread(runThread, RUN_STATUS_OK);
    }
    else if (runSemaphore->runCount < runSemaphore->runMaximum)
    {
        runSemaphore->runCount++;
    }
    else
    {
        runStatus = RUN_STATUS_FULL;
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

RUN_STATIC RUN_VOID run_thread_add_held_mutex(RunThread *runThread, RunMutex *runMutex)
{
    runMutex->runNext = runThread->runHeldMutexes;
    runThread->runHeldMutexes = runMutex;
}

RUN_STATIC RUN_VOID run_thread_remove_held_mutex(RunThread *runThread, RunMutex *runMutex)
{
    RunMutex *runCurrent = runThread->runHeldMutexes;
    RunMutex *runPrevious = RUN_NULL;
    while (runCurrent != RUN_NULL)
    {
        if (runCurrent == runMutex)
        {
            if (runPrevious == RUN_NULL)
            {
                runThread->runHeldMutexes = runCurrent->runNext;
            }
            else
            {
                runPrevious->runNext = runCurrent->runNext;
            }
            runMutex->runNext = RUN_NULL;
            return;
        }
        runPrevious = runCurrent;
        runCurrent = runCurrent->runNext;
    }
}

RUN_STATIC RUN_VOID run_thread_recompute_priority(RunThread *runThread)
{
    RUN_UINT_8 runPriority = runThread->runBasePriority;
    RunMutex *runMutex = runThread->runHeldMutexes;
    /* Recompute instead of merely restoring base priority: multiple mutexes may
     * still have waiters with different inherited priorities.
     */
    while (runMutex != RUN_NULL)
    {
        RunThread *runWaiter = runMutex->runWaiters.runFirst;
        while (runWaiter != RUN_NULL)
        {
            if (runWaiter->runPriority > runPriority)
            {
                runPriority = runWaiter->runPriority;
            }
            runWaiter = runWaiter->runWaitNext;
        }
        runMutex = runMutex->runNext;
    }
    runThread->runPriority = runPriority;
}

RunStatus run_mutex_create(RUN_CONST RunMutexConfig *runConfig, RunMutex **runMutexOut)
{
    RunMutex *runMutex;
    RunStatus runStatus;
    RunCriticalState runState = run_arch_enter_critical();
    runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runMutexOut == RUN_NULL))
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runStatus = run_memory_allocate(RunKernel.runMemory, sizeof(RunMutex),
                                    RUN_MEMORY_ALIGNMENT, (RUN_VOID **)&runMutex);
    if (runStatus == RUN_STATUS_OK)
    {
        run_memory_set(runMutex, 0, sizeof(RunMutex));
        runMutex->runMagic = RUN_SYNC_MAGIC_MUTEX;
        runMutex->runRecursive = (runConfig != RUN_NULL) ? runConfig->runRecursive : RUN_FALSE;
        *runMutexOut = runMutex;
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_mutex_delete(RunMutex *runMutex)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runMutex == RUN_NULL) ||
        (runMutex->runMagic != RUN_SYNC_MAGIC_MUTEX))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    if ((runMutex->runOwner != RUN_NULL) || (runMutex->runWaiters.runFirst != RUN_NULL))
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_BUSY;
    }
    runMutex->runMagic = 0U;
    runStatus = run_memory_release(RunKernel.runMemory, runMutex);
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_mutex_lock(RunMutex *runMutex, RunTimeMs runTimeout)
{
    RunThread *runCurrent;
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runMutex == RUN_NULL) ||
        (runMutex->runMagic != RUN_SYNC_MAGIC_MUTEX))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runCurrent = run_thread_current();
    runState = run_arch_enter_critical();
    if (runMutex->runOwner == RUN_NULL)
    {
        runMutex->runOwner = runCurrent;
        runMutex->runRecursion = 1U;
        run_thread_add_held_mutex(runCurrent, runMutex);
        run_arch_exit_critical(runState);
        return RUN_STATUS_OK;
    }
    /* Recursion is tracked per owner; non-recursive relocking returns BUSY. */
    if ((runMutex->runOwner == runCurrent) && runMutex->runRecursive)
    {
        runMutex->runRecursion++;
        run_arch_exit_critical(runState);
        return RUN_STATUS_OK;
    }
    if (runTimeout == RUN_TIME_NO_WAIT)
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_BUSY;
    }
    /* Raise immediately so the owner can release the mutex that blocks us. */
    if (runCurrent->runPriority > runMutex->runOwner->runPriority)
    {
        runMutex->runOwner->runPriority = runCurrent->runPriority;
    }
    run_arch_exit_critical(runState);
    return run_kernel_block_current(runTimeout, &runMutex->runWaiters, RUN_NULL);
}

RunStatus run_mutex_unlock(RunMutex *runMutex)
{
    RunThread *runCurrent;
    RunThread *runNext;
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runMutex == RUN_NULL) ||
        (runMutex->runMagic != RUN_SYNC_MAGIC_MUTEX))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runCurrent = run_thread_current();
    runState = run_arch_enter_critical();
    if (runMutex->runOwner != runCurrent)
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_NOT_OWNER;
    }
    if (runMutex->runRecursion > 1U)
    {
        runMutex->runRecursion--;
        run_arch_exit_critical(runState);
        return RUN_STATUS_OK;
    }
    /* Remove ownership bookkeeping before transferring to a waiter. */
    run_thread_remove_held_mutex(runCurrent, runMutex);
    run_thread_recompute_priority(runCurrent);
    runNext = run_kernel_wait_list_pop(&runMutex->runWaiters);
    /*
     * Transfer ownership before making the waiter ready. Its post-wakeup lock
     * retry then observes the mutex as already owned by the same FIFO waiter.
     */
    if (runNext != RUN_NULL)
    {
        runMutex->runOwner = runNext;
        runMutex->runRecursion = 1U;
        run_thread_add_held_mutex(runNext, runMutex);
        run_kernel_ready_thread(runNext, RUN_STATUS_OK);
    }
    else
    {
        runMutex->runOwner = RUN_NULL;
        runMutex->runRecursion = 0U;
    }
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

RunStatus run_queue_create(RUN_SIZE runItemSize,
                           RUN_UINT_32 runCapacity,
                           RunQueue **runQueueOut)
{
    RunQueue *runQueue;
    RUN_VOID *runStorage;
    RunStatus runStatus;
    RunCriticalState runState;
    if ((runQueueOut == RUN_NULL) || (runItemSize == 0U) || (runCapacity == 0U) ||
        (runItemSize > (RUN_SIZE_MAX / runCapacity)))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runStatus = run_kernel_api_enter(RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runState = run_arch_enter_critical();
    runStatus = run_memory_allocate(RunKernel.runMemory, sizeof(RunQueue),
                                    RUN_MEMORY_ALIGNMENT, (RUN_VOID **)&runQueue);
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_memory_allocate(RunKernel.runMemory, runItemSize * runCapacity,
                                        RUN_MEMORY_ALIGNMENT, &runStorage);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        run_memory_set(runQueue, 0, sizeof(RunQueue));
        runQueue->runMagic = RUN_SYNC_MAGIC_QUEUE;
        runQueue->runStorage = (RUN_UINT_8 *)runStorage;
        runQueue->runItemSize = runItemSize;
        runQueue->runCapacity = runCapacity;
        *runQueueOut = runQueue;
    }
    else if (runQueue != RUN_NULL)
    {
        (RUN_VOID)run_memory_release(RunKernel.runMemory, runQueue);
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_queue_delete(RunQueue *runQueue)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runQueue == RUN_NULL) ||
        (runQueue->runMagic != RUN_SYNC_MAGIC_QUEUE))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    if ((runQueue->runSenders.runFirst != RUN_NULL) ||
        (runQueue->runReceivers.runFirst != RUN_NULL))
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_BUSY;
    }
    runQueue->runMagic = 0U;
    (RUN_VOID)run_memory_release(RunKernel.runMemory, runQueue->runStorage);
    runStatus = run_memory_release(RunKernel.runMemory, runQueue);
    run_arch_exit_critical(runState);
    return runStatus;
}

RUN_STATIC RUN_VOID run_queue_write(RunQueue *runQueue, RUN_CONST RUN_VOID *runItem)
{
    /* Count is the source of truth; head need not be reset on wrap. */
    RUN_UINT_32 runTail = (runQueue->runHead + runQueue->runCount) % runQueue->runCapacity;
    run_memory_copy(runQueue->runStorage + (runTail * runQueue->runItemSize),
                    runItem, runQueue->runItemSize);
    runQueue->runCount++;
}

RUN_STATIC RUN_VOID run_queue_read(RunQueue *runQueue, RUN_VOID *runItem)
{
    run_memory_copy(runItem,
                    runQueue->runStorage + (runQueue->runHead * runQueue->runItemSize),
                    runQueue->runItemSize);
    runQueue->runHead = (runQueue->runHead + 1U) % runQueue->runCapacity;
    runQueue->runCount--;
}

RunStatus run_queue_send(RunQueue *runQueue,
                         RUN_CONST RUN_VOID *runItem,
                         RunTimeMs runTimeout)
{
    RunThread *runThread;
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runQueue == RUN_NULL) || (runItem == RUN_NULL) ||
        (runQueue->runMagic != RUN_SYNC_MAGIC_QUEUE))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    /*
     * The loop is deliberate. Waking only means state may have changed, so a
     * sender must compete for capacity again before copying its message.
     */
    for (;;)
    {
        runState = run_arch_enter_critical();
        if (runQueue->runCount < runQueue->runCapacity)
        {
            run_queue_write(runQueue, runItem);
            /* The queued copy guarantees the awakened receiver can make progress. */
            runThread = run_kernel_wait_list_pop(&runQueue->runReceivers);
            if (runThread != RUN_NULL)
            {
                run_kernel_ready_thread(runThread, RUN_STATUS_OK);
            }
            run_arch_exit_critical(runState);
            return RUN_STATUS_OK;
        }
        run_arch_exit_critical(runState);
        if (runTimeout == RUN_TIME_NO_WAIT)
        {
            return RUN_STATUS_FULL;
        }
        runStatus = run_kernel_block_current(runTimeout, &runQueue->runSenders, RUN_NULL);
        if (runStatus != RUN_STATUS_OK)
        {
            return runStatus;
        }
    }
}

RunStatus run_queue_send_from_isr(RunQueue *runQueue, RUN_CONST RUN_VOID *runItem)
{
    RunThread *runThread;
    RunCriticalState runState;
    if ((runQueue == RUN_NULL) || (runItem == RUN_NULL) ||
        (runQueue->runMagic != RUN_SYNC_MAGIC_QUEUE))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    if (runQueue->runCount >= runQueue->runCapacity)
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_FULL;
    }
    run_queue_write(runQueue, runItem);
    runThread = run_kernel_wait_list_pop(&runQueue->runReceivers);
    if (runThread != RUN_NULL)
    {
        run_kernel_ready_thread(runThread, RUN_STATUS_OK);
    }
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

RunStatus run_queue_receive(RunQueue *runQueue, RUN_VOID *runItem, RunTimeMs runTimeout)
{
    RunThread *runThread;
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runQueue == RUN_NULL) || (runItem == RUN_NULL) ||
        (runQueue->runMagic != RUN_SYNC_MAGIC_QUEUE))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    /* As with send, successful wakeup is not permission to read without a recheck. */
    for (;;)
    {
        runState = run_arch_enter_critical();
        if (runQueue->runCount > 0U)
        {
            run_queue_read(runQueue, runItem);
            runThread = run_kernel_wait_list_pop(&runQueue->runSenders);
            if (runThread != RUN_NULL)
            {
                run_kernel_ready_thread(runThread, RUN_STATUS_OK);
            }
            run_arch_exit_critical(runState);
            return RUN_STATUS_OK;
        }
        run_arch_exit_critical(runState);
        if (runTimeout == RUN_TIME_NO_WAIT)
        {
            return RUN_STATUS_EMPTY;
        }
        runStatus = run_kernel_block_current(runTimeout, &runQueue->runReceivers, RUN_NULL);
        if (runStatus != RUN_STATUS_OK)
        {
            return runStatus;
        }
    }
}

RunStatus run_queue_receive_from_isr(RunQueue *runQueue, RUN_VOID *runItem)
{
    RunThread *runThread;
    RunCriticalState runState;
    if ((runQueue == RUN_NULL) || (runItem == RUN_NULL) ||
        (runQueue->runMagic != RUN_SYNC_MAGIC_QUEUE))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    if (runQueue->runCount == 0U)
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_EMPTY;
    }
    run_queue_read(runQueue, runItem);
    runThread = run_kernel_wait_list_pop(&runQueue->runSenders);
    if (runThread != RUN_NULL)
    {
        run_kernel_ready_thread(runThread, RUN_STATUS_OK);
    }
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

RunStatus run_queue_peek(RunQueue *runQueue, RUN_VOID *runItem)
{
    RunCriticalState runState;
    RunStatus runStatus = RUN_STATUS_OK;
    if ((runQueue == RUN_NULL) || (runItem == RUN_NULL) ||
        (runQueue->runMagic != RUN_SYNC_MAGIC_QUEUE))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    if (runQueue->runCount == 0U)
    {
        runStatus = RUN_STATUS_EMPTY;
    }
    else
    {
        run_memory_copy(runItem,
                        runQueue->runStorage + (runQueue->runHead * runQueue->runItemSize),
                        runQueue->runItemSize);
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_event_group_create(RunEventGroup **runEventGroupOut)
{
    RunEventGroup *runEventGroup;
    RunStatus runStatus;
    RunCriticalState runState;
    if (runEventGroupOut == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runStatus = run_kernel_api_enter(RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runState = run_arch_enter_critical();
    runStatus = run_memory_allocate(RunKernel.runMemory, sizeof(RunEventGroup),
                                    RUN_MEMORY_ALIGNMENT, (RUN_VOID **)&runEventGroup);
    if (runStatus == RUN_STATUS_OK)
    {
        run_memory_set(runEventGroup, 0, sizeof(RunEventGroup));
        runEventGroup->runMagic = RUN_SYNC_MAGIC_EVENT;
        *runEventGroupOut = runEventGroup;
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_event_group_delete(RunEventGroup *runEventGroup)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runEventGroup == RUN_NULL) ||
        (runEventGroup->runMagic != RUN_SYNC_MAGIC_EVENT))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    run_sync_destroy_waiters(&runEventGroup->runWaiters, RUN_STATUS_CANCELLED);
    runEventGroup->runMagic = 0U;
    runStatus = run_memory_release(RunKernel.runMemory, runEventGroup);
    run_arch_exit_critical(runState);
    return runStatus;
}

RUN_STATIC RUN_VOID run_event_group_awaken(RunEventGroup *runEventGroup)
{
    RunThread *runThread;
    /*
     * Wake every observer because different tasks may wait for different bits.
     * Each task re-evaluates its own all/any predicate after resuming.
     */
    while ((runThread = run_kernel_wait_list_pop(&runEventGroup->runWaiters)) != RUN_NULL)
    {
        run_kernel_ready_thread(runThread, RUN_STATUS_OK);
    }
}

RunStatus run_event_group_set(RunEventGroup *runEventGroup, RUN_UINT_32 runBits)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runEventGroup == RUN_NULL) ||
        (runEventGroup->runMagic != RUN_SYNC_MAGIC_EVENT))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    runEventGroup->runBits |= runBits;
    run_event_group_awaken(runEventGroup);
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

RunStatus run_event_group_set_from_isr(RunEventGroup *runEventGroup, RUN_UINT_32 runBits)
{
    RunCriticalState runState = run_arch_enter_critical();
    RunStatus runStatus = RUN_STATUS_OK;
    if ((runEventGroup == RUN_NULL) ||
        (runEventGroup->runMagic != RUN_SYNC_MAGIC_EVENT))
    {
        runStatus = RUN_STATUS_INVALID_ARGUMENT;
    }
    else
    {
        runEventGroup->runBits |= runBits;
        run_event_group_awaken(runEventGroup);
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_event_group_clear(RunEventGroup *runEventGroup, RUN_UINT_32 runBits)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runEventGroup == RUN_NULL) ||
        (runEventGroup->runMagic != RUN_SYNC_MAGIC_EVENT))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    runEventGroup->runBits &= ~runBits;
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

RunStatus run_event_group_wait(RunEventGroup *runEventGroup,
                               RUN_UINT_32 runBits,
                               RUN_BOOL runWaitAll,
                               RUN_BOOL runClearOnExit,
                               RUN_UINT_32 *runObservedBits,
                               RunTimeMs runTimeout)
{
    RunStatus runStatus;
    RunCriticalState runState;
    if ((runEventGroup == RUN_NULL) || (runBits == 0U) ||
        (runEventGroup->runMagic != RUN_SYNC_MAGIC_EVENT))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runStatus = run_kernel_api_enter(RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    for (;;)
    {
        runState = run_arch_enter_critical();
        RUN_UINT_32 runMatched = (runWaitAll) ?
            ((runEventGroup->runBits & runBits) == runBits) :
            ((runEventGroup->runBits & runBits) != 0U);
        if (runMatched)
        {
            RUN_UINT_32 runObserved = runEventGroup->runBits;
            if (runClearOnExit)
            {
                runEventGroup->runBits &= ~runBits;
            }
            if (runObservedBits != RUN_NULL)
            {
                *runObservedBits = runObserved;
            }
            run_arch_exit_critical(runState);
            return RUN_STATUS_OK;
        }
        run_arch_exit_critical(runState);
        if (runTimeout == RUN_TIME_NO_WAIT)
        {
            return RUN_STATUS_TIMEOUT;
        }
        runStatus = run_kernel_block_current(runTimeout,
                                             &runEventGroup->runWaiters,
                                             RUN_NULL);
        if (runStatus != RUN_STATUS_OK)
        {
            return runStatus;
        }
    }
}

RUN_STATIC RUN_VOID run_timer_insert(RunTimer *runTimer)
{
    RunTimer *runCurrent = RunTimerFirst;
    RunTimer *runPrevious = RUN_NULL;
    /* Due times use the same signed wrap comparison as thread timeouts. */
    while ((runCurrent != RUN_NULL) &&
           ((RUN_INT_32)(runCurrent->runDue - runTimer->runDue) <= 0))
    {
        runPrevious = runCurrent;
        runCurrent = runCurrent->runNext;
    }
    runTimer->runNext = runCurrent;
    if (runPrevious == RUN_NULL)
    {
        RunTimerFirst = runTimer;
    }
    else
    {
        runPrevious->runNext = runTimer;
    }
}

RUN_STATIC RUN_VOID run_timer_remove(RunTimer *runTimer)
{
    RunTimer *runCurrent = RunTimerFirst;
    RunTimer *runPrevious = RUN_NULL;
    while (runCurrent != RUN_NULL)
    {
        if (runCurrent == runTimer)
        {
            if (runPrevious == RUN_NULL)
            {
                RunTimerFirst = runCurrent->runNext;
            }
            else
            {
                runPrevious->runNext = runCurrent->runNext;
            }
            runTimer->runNext = RUN_NULL;
            runTimer->runRunning = RUN_FALSE;
            return;
        }
        runPrevious = runCurrent;
        runCurrent = runCurrent->runNext;
    }
}

RunStatus run_timer_create(RunTimerCallback runCallback,
                           RUN_VOID *runArgument,
                           RUN_BOOL runPeriodic,
                           RunTimeMs runPeriod,
                           RunTimer **runTimerOut)
{
    RunTimer *runTimer;
    RunStatus runStatus;
    RUN_UINT_64 runTicksValue;
    RunCriticalState runState;
    if ((runCallback == RUN_NULL) || (runTimerOut == RUN_NULL) || (runPeriod == 0U))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runTicksValue = (runPeriod * RUN_CONFIG_TICK_HZ + 999U) / 1000U;
    if (runTicksValue == 0U)
    {
        runTicksValue = 1U;
    }
    if (runTicksValue > 0x7FFFFFFFU)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runStatus = run_kernel_api_enter(RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runState = run_arch_enter_critical();
    runStatus = run_memory_allocate(RunKernel.runMemory, sizeof(RunTimer),
                                    RUN_MEMORY_ALIGNMENT, (RUN_VOID **)&runTimer);
    if (runStatus == RUN_STATUS_OK)
    {
        run_memory_set(runTimer, 0, sizeof(RunTimer));
        runTimer->runMagic = RUN_SYNC_MAGIC_TIMER;
        runTimer->runCallback = runCallback;
        runTimer->runArgument = runArgument;
        runTimer->runPeriodic = runPeriodic;
        runTimer->runPeriod = runPeriod;
        *runTimerOut = runTimer;
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_timer_delete(RunTimer *runTimer)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runTimer == RUN_NULL) ||
        (runTimer->runMagic != RUN_SYNC_MAGIC_TIMER))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    run_timer_remove(runTimer);
    runTimer->runMagic = 0U;
    runStatus = run_memory_release(RunKernel.runMemory, runTimer);
    run_arch_exit_critical(runState);
    return runStatus;
}

RunStatus run_timer_start(RunTimer *runTimer, RunTimeMs runDelay)
{
    RunCriticalState runState;
    RUN_UINT_64 runTicksValue;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runTimer == RUN_NULL) ||
        (runTimer->runMagic != RUN_SYNC_MAGIC_TIMER))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runTicksValue = (runDelay * RUN_CONFIG_TICK_HZ + 999U) / 1000U;
    if (runTicksValue == 0U)
    {
        runTicksValue = 1U;
    }
    if (runTicksValue > 0x7FFFFFFFU)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    /* Restarting removes any old queue node before installing a new deadline. */
    run_timer_remove(runTimer);
    runTimer->runDue = (RunTick)RunKernel.runTicks + (RunTick)runTicksValue;
    runTimer->runRunning = RUN_TRUE;
    run_timer_insert(runTimer);
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

RunStatus run_timer_stop(RunTimer *runTimer)
{
    RunCriticalState runState;
    RunStatus runStatus = run_kernel_api_enter(RUN_FALSE);
    if ((runStatus != RUN_STATUS_OK) || (runTimer == RUN_NULL) ||
        (runTimer->runMagic != RUN_SYNC_MAGIC_TIMER))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    run_timer_remove(runTimer);
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

