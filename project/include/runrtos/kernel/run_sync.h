/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - synchronization, queues, events, and software timers
 *
 * Layer: kernel
 * Purpose: Publish portable IPC and timer contracts built on the scheduler.
 * Dependencies: run_kernel.h and the kernel wait/timeout implementation.
 * Constraint: Objects are dynamically owned; blocking calls require thread
 *             context and ISR-safe variants must be used from interrupt handlers.
 */
#ifndef RUN_SYNC_H_
#define RUN_SYNC_H_

#include "runrtos/kernel/run_kernel.h"

typedef RUN_STRUCT RunSemaphore RunSemaphore;
typedef RUN_STRUCT RunMutex RunMutex;
typedef RUN_STRUCT RunQueue RunQueue;
typedef RUN_STRUCT RunEventGroup RunEventGroup;
typedef RUN_STRUCT RunTimer RunTimer;

typedef RUN_VOID (*RunTimerCallback)(RunTimer *runTimer, RUN_VOID *runArgument);

/* Read-only timer snapshot suitable for diagnostics outside the kernel. */
typedef RUN_STRUCT RunTimerInfo
{
    RunTimer *runTimer;
    RUN_UINT_32 runState;
    RUN_BOOL runPeriodic;
    RUN_BOOL runRunning;
    RunTimeMs runPeriod;
    RunTimeMs runRemaining;
    RUN_UINT_64 runCallbackCount;
} RunTimerInfo;

typedef RUN_STRUCT RunMutexConfig
{
    RUN_BOOL runRecursive;
} RunMutexConfig;

/*
 * Creates the timer service thread. Call once from thread context after kernel
 * initialization and before creating any software timer.
 */
RunStatus run_timer_system_init(RUN_VOID);

/*
 * Counting semaphore. Take consumes one unit or waits for a unit; give either
 * hands one unit directly to the oldest waiter or increments the count. A give
 * beyond maximum returns RUN_STATUS_FULL. Deletion cancels all waiters.
 */
RunStatus run_semaphore_create(RUN_UINT_32 runInitialCount,
                               RUN_UINT_32 runMaximumCount,
                               RunSemaphore **runSemaphoreOut);
RunStatus run_semaphore_delete(RunSemaphore *runSemaphore);
RunStatus run_semaphore_take(RunSemaphore *runSemaphore, RunTimeMs runTimeout);
RunStatus run_semaphore_take_from_isr(RunSemaphore *runSemaphore);
RunStatus run_semaphore_give(RunSemaphore *runSemaphore);
RunStatus run_semaphore_give_from_isr(RunSemaphore *runSemaphore);

/*
 * Mutex ownership is transferred directly to the oldest waiter on unlock.
 * Waiting raises the owner's effective priority to the highest waiter priority;
 * unlock recomputes it from the owner's remaining held mutexes. A recursive
 * mutex permits nested locks only by its current owner. Deletion is rejected
 * while the mutex is owned or has waiters.
 */
RunStatus run_mutex_create(RUN_CONST RunMutexConfig *runConfig, RunMutex **runMutexOut);
RunStatus run_mutex_delete(RunMutex *runMutex);
RunStatus run_mutex_lock(RunMutex *runMutex, RunTimeMs runTimeout);
RunStatus run_mutex_unlock(RunMutex *runMutex);

/*
 * Fixed-element circular queue. Each operation copies exactly runItemSize bytes,
 * so callers do not retain ownership of message payloads. A blocked sender or
 * receiver is only made ready; after wakeup the operation rechecks queue state
 * before proceeding. ISR variants never block and report FULL or EMPTY instead.
 */
RunStatus run_queue_create(RUN_SIZE runItemSize,
                           RUN_UINT_32 runCapacity,
                           RunQueue **runQueueOut);
RunStatus run_queue_delete(RunQueue *runQueue);
RunStatus run_queue_send(RunQueue *runQueue,
                         RUN_CONST RUN_VOID *runItem,
                         RunTimeMs runTimeout);
RunStatus run_queue_send_from_isr(RunQueue *runQueue, RUN_CONST RUN_VOID *runItem);
RunStatus run_queue_receive(RunQueue *runQueue, RUN_VOID *runItem, RunTimeMs runTimeout);
RunStatus run_queue_receive_from_isr(RunQueue *runQueue, RUN_VOID *runItem);
RunStatus run_queue_peek(RunQueue *runQueue, RUN_VOID *runItem);

/*
 * A wait succeeds when any requested bit is set (runWaitAll is false) or when
 * all requested bits are set (runWaitAll is true). runBits is only modified on
 * success when runClearOnExit is true. Waiters re-evaluate their condition after
 * each wakeup.
 */
RunStatus run_event_group_create(RunEventGroup **runEventGroupOut);
RunStatus run_event_group_delete(RunEventGroup *runEventGroup);
RunStatus run_event_group_set(RunEventGroup *runEventGroup, RUN_UINT_32 runBits);
RunStatus run_event_group_set_from_isr(RunEventGroup *runEventGroup, RUN_UINT_32 runBits);
RunStatus run_event_group_clear(RunEventGroup *runEventGroup, RUN_UINT_32 runBits);
RunStatus run_event_group_wait(RunEventGroup *runEventGroup,
                               RUN_UINT_32 runBits,
                               RUN_BOOL runWaitAll,
                               RUN_BOOL runClearOnExit,
                               RUN_UINT_32 *runObservedBits,
                               RunTimeMs runTimeout);

/*
 * Creates a stopped software timer. run_timer_start supplies the first delay; a
 * periodic timer then uses its configured nonzero runPeriod for later
 * expirations. One-shot timers may use zero because their delay comes from
 * run_timer_start.
 * Callbacks run in the timer service thread and must not block. start, stop, and
 * delete are legal from the callback; deletion is deferred until the callback
 * returns, and deletion from another thread during the callback is also safe.
 */
RunStatus run_timer_create(RunTimerCallback runCallback,
                           RUN_VOID *runArgument,
                           RUN_BOOL runPeriodic,
                           RunTimeMs runPeriod,
                           RunTimer **runTimerOut);
RunStatus run_timer_delete(RunTimer *runTimer);
RunStatus run_timer_start(RunTimer *runTimer, RunTimeMs runDelay);
RunStatus run_timer_stop(RunTimer *runTimer);

/*
 * Enumerates all live timers in creation order and returns a point-in-time
 * snapshot. A stale index returns INVALID_ARGUMENT. Repeated calls are not a
 * transactional iterator because timers may be created or deleted concurrently.
 */
RunStatus run_timer_get_info(RUN_SIZE runIndex, RunTimerInfo *runInfoOut);

#endif

