/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - synchronization, queue, event, and timer tests
 *
 * Layer: tests/host
 * Purpose: Verify concurrent waits, timeout wakes, priority inheritance, and
 *          timer lifecycle changes made from callbacks and another thread.
 * Dependencies: host scheduler, public kernel synchronization APIs, and timers.
 * Constraint: Timer callback test state is static and remains valid until the
 *             service thread finishes deferred deletion.
 */
#include "test_harness.h"
#include "runrtos/kernel/run_sync.h"

RUN_STATIC RunStatus RunSyncWorkerStatus;
RUN_STATIC RUN_UINT_32 RunSyncWorkerValue;
RUN_STATIC RunSemaphore *RunSyncDone;
RUN_STATIC RUN_CHAR RunMutexTrace[16];
RUN_STATIC RUN_UINT_32 RunMutexTraceLength;

typedef RUN_STRUCT RunSyncSemaphoreWorker
{
    RunSemaphore *runSemaphore;
    RunTimeMs runTimeout;
} RunSyncSemaphoreWorker;

typedef RUN_STRUCT RunSyncQueueWorker
{
    RunQueue *runQueue;
    RUN_UINT_32 *runValueOut;
} RunSyncQueueWorker;

typedef RUN_STRUCT RunSyncEventWorker
{
    RunEventGroup *runEventGroup;
} RunSyncEventWorker;

typedef RUN_STRUCT RunSyncMutexWorker
{
    RunMutex *runMutex;
} RunSyncMutexWorker;

typedef RUN_STRUCT RunSyncTimerState
{
    RunTimer *runSelf;
    RunSemaphore *runStarted;
    RunSemaphore *runFinished;
    RUN_UINT_32 runCount;
    RUN_UINT_32 runStopAfter;
    RUN_UINT_32 runStartAgain;
    RUN_BOOL runDeleteSelf;
} RunSyncTimerState;

/* Appends one synchronization ordering marker to the test trace. */
RUN_STATIC RUN_VOID run_sync_trace(RUN_CHAR runValue)
{
    if (RunMutexTraceLength < sizeof(RunMutexTrace))
    {
        RunMutexTrace[RunMutexTraceLength++] = runValue;
    }
}

/* Waits on a semaphore to exercise timeout and wakeup behavior. */
RUN_STATIC RUN_VOID run_sync_semaphore_worker(RUN_VOID *runArgument)
{
    RunSyncSemaphoreWorker *runWorker =
        (RunSyncSemaphoreWorker *)runArgument;
    RunSyncWorkerStatus =
        run_semaphore_take(runWorker->runSemaphore, runWorker->runTimeout);
    (RUN_VOID)run_semaphore_give(RunSyncDone);
}

/* Produces queue values for the producer-consumer test. */
RUN_STATIC RUN_VOID run_sync_queue_worker(RUN_VOID *runArgument)
{
    RunSyncQueueWorker *runWorker = (RunSyncQueueWorker *)runArgument;
    RunSyncWorkerStatus =
        run_queue_receive(runWorker->runQueue, runWorker->runValueOut, 100U);
    (RUN_VOID)run_semaphore_give(RunSyncDone);
}

/* Waits for event bits configured by the event-group tests. */
RUN_STATIC RUN_VOID run_sync_event_worker(RUN_VOID *runArgument)
{
    RunSyncEventWorker *runWorker = (RunSyncEventWorker *)runArgument;
    RunSyncWorkerValue = 0U;
    RunSyncWorkerStatus = run_event_group_wait(
        runWorker->runEventGroup, 0x3U, RUN_TRUE, RUN_TRUE,
        &RunSyncWorkerValue, 100U);
    (RUN_VOID)run_semaphore_give(RunSyncDone);
}

/* Holds a mutex to expose lower-priority ownership in inheritance tests. */
RUN_STATIC RUN_VOID run_sync_mutex_low(RUN_VOID *runArgument)
{
    RunSyncMutexWorker *runWorker = (RunSyncMutexWorker *)runArgument;
    (RUN_VOID)run_mutex_lock(runWorker->runMutex, 100U);
    run_sync_trace('L');
    (RUN_VOID)run_semaphore_give(RunSyncDone);
    (RUN_VOID)run_thread_sleep(3U);
    run_sync_trace('r');
    (RUN_VOID)run_mutex_unlock(runWorker->runMutex);
    (RUN_VOID)run_semaphore_give(RunSyncDone);
}

/* Emits a priority marker while a lower-priority owner awaits release. */
RUN_STATIC RUN_VOID run_sync_mutex_medium(RUN_VOID *runArgument)
{
    RunSyncMutexWorker *runWorker = (RunSyncMutexWorker *)runArgument;
    run_sync_trace('M');
    RunSyncWorkerStatus = run_mutex_lock(runWorker->runMutex, 100U);
    if (RunSyncWorkerStatus == RUN_STATUS_OK)
    {
        run_sync_trace('A');
        (RUN_VOID)run_mutex_unlock(runWorker->runMutex);
    }
    (RUN_VOID)run_semaphore_give(RunSyncDone);
}

/* Mutates timer lifecycle state from inside its own service callback. */
RUN_STATIC RUN_VOID run_sync_timer_callback(RunTimer *runTimer,
                                            RUN_VOID *runArgument)
{
    RunSyncTimerState *runState = (RunSyncTimerState *)runArgument;
    runState->runCount++;
    if (runState->runStarted != RUN_NULL)
    {
        (RUN_VOID)run_semaphore_give(runState->runStarted);
    }
    if (runState->runDeleteSelf)
    {
        (RUN_VOID)run_timer_delete(runTimer);
    }
    else if (runState->runCount < runState->runStartAgain)
    {
        (RUN_VOID)run_timer_start(runTimer, 1U);
    }
    else if ((runState->runStopAfter != 0U) &&
             (runState->runCount >= runState->runStopAfter))
    {
        (RUN_VOID)run_timer_stop(runTimer);
    }
    if (runState->runFinished != RUN_NULL)
    {
        (RUN_VOID)run_semaphore_give(runState->runFinished);
    }
}

/* Exercises IPC, mutex priority inheritance, and timer lifecycle rules. */
RUN_VOID run_test_sync_suite(RunTestContext *runContext,
                             RunSemaphore *runKickSemaphore)
{
    RunThreadConfig runConfig = {
        "sync-worker", RUN_NULL, RUN_NULL, 1024U, 5U, 1U
    };
    RunThread *runThread = RUN_NULL;
    RunSemaphore *runSemaphore = RUN_NULL;
    RunSemaphore *runDeleted = RUN_NULL;
    RunQueue *runQueue = RUN_NULL;
    RunEventGroup *runEventGroup = RUN_NULL;
    RunMutex *runMutex = RUN_NULL;
    RunTimer *runTimer = RUN_NULL;
    RunTimerInfo runTimerInfo;
    RunMutexConfig runMutexConfig;
    RunSyncSemaphoreWorker runSemaphoreWorker;
    RunSyncQueueWorker runQueueWorker;
    RunSyncEventWorker runEventWorker;
    RunSyncMutexWorker runMutexLowWorker;
    RunSyncMutexWorker runMutexMediumWorker;
    RunSyncTimerState runTimerState;
    RUN_UINT_32 runQueueValue = 0U;
    RUN_UINT_32 runSentValue = 0x12345678U;

    (RUN_VOID)runKickSemaphore;
    run_test_suite_begin(runContext, "sync");
    RUN_TEST_STATUS(runContext, run_semaphore_create(0U, 8U, &RunSyncDone),
                    RUN_STATUS_OK);

    RUN_TEST_STATUS(runContext,
                    run_semaphore_create(0U, 1U, &runSemaphore), RUN_STATUS_OK);
    runSemaphoreWorker.runSemaphore = runSemaphore;
    runSemaphoreWorker.runTimeout = 100U;
    runConfig.runName = "sem-wait";
    runConfig.runEntry = run_sync_semaphore_worker;
    runConfig.runArgument = &runSemaphoreWorker;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_semaphore_give(runSemaphore), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunSyncDone, 100U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, RunSyncWorkerStatus, RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(runSemaphore, RUN_TIME_NO_WAIT),
                    RUN_STATUS_EMPTY);
    RUN_TEST_STATUS(runContext, run_semaphore_delete(runSemaphore), RUN_STATUS_OK);

    RUN_TEST_STATUS(runContext,
                    run_semaphore_create(0U, 1U, &runDeleted), RUN_STATUS_OK);
    runSemaphoreWorker.runSemaphore = runDeleted;
    runSemaphoreWorker.runTimeout = RUN_TIME_FOREVER;
    runConfig.runName = "sem-delete";
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_thread_sleep(1U), RUN_STATUS_TIMEOUT);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_delete(runDeleted), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunSyncDone, 100U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, RunSyncWorkerStatus, RUN_STATUS_CANCELLED);

    RUN_TEST_STATUS(runContext,
                    run_queue_create(sizeof(RUN_UINT_32), 2U, &runQueue),
                    RUN_STATUS_OK);
    runQueueWorker.runQueue = runQueue;
    runQueueWorker.runValueOut = &runQueueValue;
    runConfig.runName = "queue-receive";
    runConfig.runEntry = run_sync_queue_worker;
    runConfig.runArgument = &runQueueWorker;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_queue_send(runQueue, &runSentValue, 0U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunSyncDone, 100U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, RunSyncWorkerStatus, RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext, runQueueValue == runSentValue);
    RUN_TEST_STATUS(runContext,
                    run_queue_receive(runQueue, &runQueueValue, 0U),
                    RUN_STATUS_EMPTY);
    RUN_TEST_STATUS(runContext, run_queue_delete(runQueue), RUN_STATUS_OK);

    RUN_TEST_STATUS(runContext,
                    run_event_group_create(&runEventGroup), RUN_STATUS_OK);
    runEventWorker.runEventGroup = runEventGroup;
    runConfig.runName = "event-wait";
    runConfig.runEntry = run_sync_event_worker;
    runConfig.runArgument = &runEventWorker;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_event_group_set(runEventGroup, 0x1U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_thread_sleep(1U), RUN_STATUS_TIMEOUT);
    RUN_TEST_STATUS(runContext,
                    run_event_group_set(runEventGroup, 0x2U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunSyncDone, 100U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, RunSyncWorkerStatus, RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext, RunSyncWorkerValue == 0x3U);
    RUN_TEST_CHECK(runContext,
        run_event_group_wait(runEventGroup, 0x3U, RUN_TRUE, RUN_FALSE,
                             RUN_NULL, 0U) == RUN_STATUS_TIMEOUT);
    RUN_TEST_STATUS(runContext, run_event_group_delete(runEventGroup),
                    RUN_STATUS_OK);

    runMutexConfig.runRecursive = RUN_FALSE;
    RUN_TEST_STATUS(runContext,
                    run_mutex_create(&runMutexConfig, &runMutex), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_mutex_unlock(runMutex), RUN_STATUS_NOT_OWNER);
    RUN_TEST_STATUS(runContext, run_mutex_lock(runMutex, 0U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_mutex_lock(runMutex, 0U), RUN_STATUS_BUSY);
    RUN_TEST_STATUS(runContext, run_mutex_unlock(runMutex), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_mutex_delete(runMutex), RUN_STATUS_OK);

    runMutexConfig.runRecursive = RUN_TRUE;
    RUN_TEST_STATUS(runContext,
                    run_mutex_create(&runMutexConfig, &runMutex), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_mutex_lock(runMutex, 0U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_mutex_lock(runMutex, 0U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_mutex_unlock(runMutex), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_mutex_delete(runMutex), RUN_STATUS_BUSY);
    RUN_TEST_STATUS(runContext, run_mutex_unlock(runMutex), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_mutex_delete(runMutex), RUN_STATUS_OK);

    runMutexConfig.runRecursive = RUN_FALSE;
    RUN_TEST_STATUS(runContext,
                    run_mutex_create(&runMutexConfig, &runMutex), RUN_STATUS_OK);
    RunMutexTraceLength = 0U;
    runMutexLowWorker.runMutex = runMutex;
    runMutexMediumWorker.runMutex = runMutex;
    runConfig.runName = "mutex-low";
    runConfig.runEntry = run_sync_mutex_low;
    runConfig.runArgument = &runMutexLowWorker;
    runConfig.runPriority = 3U;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunSyncDone, 100U), RUN_STATUS_OK);
    runConfig.runName = "mutex-medium";
    runConfig.runEntry = run_sync_mutex_medium;
    runConfig.runArgument = &runMutexMediumWorker;
    runConfig.runPriority = 5U;
    RUN_TEST_STATUS(runContext,
                    run_thread_create(&runConfig, &runThread), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunSyncDone, 100U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(RunSyncDone, 100U), RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext,
        (RunMutexTraceLength == 4U) && (RunMutexTrace[0] == 'L') &&
        (RunMutexTrace[1] == 'M') && (RunMutexTrace[2] == 'r') &&
        (RunMutexTrace[3] == 'A'));
    RUN_TEST_STATUS(runContext, run_mutex_delete(runMutex), RUN_STATUS_OK);

    runTimerState.runSelf = RUN_NULL;
    runTimerState.runStarted = RUN_NULL;
    RUN_TEST_STATUS(runContext,
                    run_semaphore_create(0U, 8U, &runSemaphore), RUN_STATUS_OK);
    runTimerState.runFinished = runSemaphore;
    runTimerState.runCount = 0U;
    runTimerState.runStopAfter = 1U;
    runTimerState.runStartAgain = 0U;
    runTimerState.runDeleteSelf = RUN_FALSE;
    RUN_TEST_STATUS(runContext,
        run_timer_create(run_sync_timer_callback, &runTimerState, RUN_FALSE,
                         0U, &runTimer), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_timer_delete(runTimer), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
        run_timer_create(run_sync_timer_callback, &runTimerState, RUN_TRUE,
                         0U, &runTimer), RUN_STATUS_INVALID_ARGUMENT);
    RUN_TEST_STATUS(runContext,
        run_timer_create(run_sync_timer_callback, &runTimerState, RUN_TRUE,
                         1U, &runTimer), RUN_STATUS_OK);
    runTimerState.runSelf = runTimer;
    RUN_TEST_STATUS(runContext, run_timer_start(runTimer, 1U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(runSemaphore, 100U), RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext, runTimerState.runCount == 1U);
    RUN_TEST_STATUS(runContext, run_timer_get_info(0U, &runTimerInfo),
                    RUN_STATUS_OK);

    runTimerState.runCount = 0U;
    runTimerState.runStopAfter = 0U;
    runTimerState.runStartAgain = 3U;
    runTimerState.runDeleteSelf = RUN_FALSE;
    RUN_TEST_STATUS(runContext,
        run_timer_create(run_sync_timer_callback, &runTimerState, RUN_FALSE,
                         1U, &runTimer), RUN_STATUS_OK);
    runTimerState.runSelf = runTimer;
    RUN_TEST_STATUS(runContext, run_timer_start(runTimer, 1U), RUN_STATUS_OK);
    for (RUN_UINT_32 runIndex = 0U; runIndex < 3U; runIndex++)
    {
        RUN_TEST_STATUS(runContext,
                        run_semaphore_take(runSemaphore, 100U), RUN_STATUS_OK);
    }
    RUN_TEST_CHECK(runContext, runTimerState.runCount == 3U);
    RUN_TEST_STATUS(runContext, run_timer_delete(runTimer), RUN_STATUS_OK);

    runTimerState.runStarted = runSemaphore;
    runTimerState.runCount = 0U;
    runTimerState.runStopAfter = 0U;
    runTimerState.runStartAgain = 0U;
    runTimerState.runDeleteSelf = RUN_FALSE;
    RUN_TEST_STATUS(runContext,
        run_timer_create(run_sync_timer_callback, &runTimerState, RUN_FALSE,
                         1U, &runTimer), RUN_STATUS_OK);
    runTimerState.runSelf = runTimer;
    RUN_TEST_STATUS(runContext, run_timer_start(runTimer, 1U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(runSemaphore, 100U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_timer_delete(runTimer), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_thread_sleep(2U), RUN_STATUS_TIMEOUT);
    RUN_TEST_CHECK(runContext, runTimerState.runCount == 1U);

    runTimerState.runStarted = RUN_NULL;
    runTimerState.runCount = 0U;
    runTimerState.runDeleteSelf = RUN_TRUE;
    RUN_TEST_STATUS(runContext,
        run_timer_create(run_sync_timer_callback, &runTimerState, RUN_FALSE,
                         1U, &runTimer), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_timer_start(runTimer, 1U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
                    run_semaphore_take(runSemaphore, 100U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_thread_sleep(2U), RUN_STATUS_TIMEOUT);
    RUN_TEST_CHECK(runContext, runTimerState.runCount == 1U);

    RUN_TEST_STATUS(runContext, run_semaphore_delete(runSemaphore), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_semaphore_delete(RunSyncDone), RUN_STATUS_OK);
}
