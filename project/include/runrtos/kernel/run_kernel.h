/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - scheduler and thread public API
 *
 * Layer: kernel
 * Purpose: Define thread lifecycle, scheduling, sleep, and statistics contracts.
 * Dependencies: runtime memory/string services and architecture port.
 * Constraint: Object structures are private; ordinary blocking APIs are forbidden
 *             in ISRs unless a dedicated *_from_isr function exists.
 */
#ifndef RUN_KERNEL_H_
#define RUN_KERNEL_H_

#include "runrtos/runtime/run_memory.h"
#include "runrtos/runtime/run_string.h"

typedef RUN_STRUCT RunThread RunThread;

/* Entry code runs as a normal thread and may return or call run_thread_exit. */
typedef RUN_VOID (*RunThreadEntry)(RUN_VOID *runArgument);

typedef RUN_STRUCT RunThreadConfig
{
    /* Borrowed for the thread lifetime; copy a dynamic name before returning. */
    RUN_CONST RUN_CHAR *runName;
    RunThreadEntry runEntry;
    RUN_VOID *runArgument;
    /* Stack bytes allocated from the kernel arena, including runtime reserve. */
    RUN_SIZE runStackSize;
    /* 0 is reserved for the idle thread; valid application values start at 1. */
    RUN_UINT_8 runPriority;
    /* Zero selects one full second (RUN_CONFIG_TICK_HZ ticks). */
    RUN_UINT_32 runTimeSliceTicks;
} RunThreadConfig;

typedef RUN_STRUCT RunThreadStats
{
    RunStringView runName;
    RUN_UINT_8 runPriority;
    RUN_UINT_8 runBasePriority;
    RUN_UINT_32 runState;
    RUN_SIZE runStackSize;
    RUN_SIZE runStackHighWater;
    RUN_UINT_64 runContextSwitches;
} RunThreadStats;

/*
 * Stable snapshot for diagnostics. The handle remains kernel-owned and may
 * become stale after deletion; do not dereference it directly.
 */
typedef RUN_STRUCT RunThreadInfo
{
    RunThread *runThread;
    RunThreadStats runStats;
} RunThreadInfo;

typedef RUN_STRUCT RunKernelStats
{
    RUN_UINT_64 runTicks;
    RUN_UINT_32 runReadyCount;
    RUN_UINT_64 runContextSwitches;
    RUN_UINT_32 runThreadCount;
} RunKernelStats;

/*
 * Initializes scheduler state from an already initialized allocator. This does
 * not allocate or start the idle thread; call run_scheduler_start after creating
 * application threads and initializing optional kernel services.
 */
RunStatus run_kernel_init(RunMemory *runMemory);

/*
 * Creates a ready thread from thread context. The thread object and stack are
 * owned by the kernel. The caller must keep the borrowed name and argument
 * valid for as long as the thread may use them.
 */
RunStatus run_thread_create(RUN_CONST RunThreadConfig *runConfig, RunThread **runThreadOut);

/*
 * Asynchronously terminates a thread. Deleting the caller frees its resources
 * during the next context switch; deleting another thread frees immediately.
 * The idle thread cannot be deleted.
 */
RunStatus run_thread_delete(RunThread *runThread);
RunThread *run_thread_current(RUN_VOID);

/* Starts the scheduler and never returns to the caller. */
RUN_VOID run_scheduler_start(RUN_VOID) RUN_ATTR_NORETURN;

/* Requests a reschedule at the next interrupt-safe opportunity. */
RUN_VOID run_scheduler_yield(RUN_VOID);

/*
 * Blocks for at least the requested duration (rounded up to a tick). A value of
 * zero yields; RUN_TIME_FOREVER is not meaningful for an ordinary sleep.
 */
RunStatus run_thread_sleep(RunTimeMs runDuration);

/* Suspends or resumes an existing thread; both calls require thread context. */
RunStatus run_thread_suspend(RunThread *runThread);
RunStatus run_thread_resume(RunThread *runThread);

/* Terminates the current thread and never returns to it. */
RUN_VOID run_thread_exit(RUN_VOID) RUN_ATTR_NORETURN;

/* Must be called from the architecture tick handler, never from thread code. */
RUN_VOID run_kernel_tick_from_isr(RUN_VOID);
RUN_BOOL run_kernel_is_in_isr(RUN_VOID);

/* Statistics are snapshots; output pointers must be non-null. */
RUN_VOID run_kernel_get_stats(RunKernelStats *runStats);
RunStatus run_thread_get_stats(RunThread *runThread, RunThreadStats *runStats);

/*
 * Enumerates threads in scheduler order while taking a consistent snapshot.
 * runIndex is zero based. The order is private and may change after scheduling.
 * This call is safe in thread context and returns INVALID_ARGUMENT for a stale
 * index or null output.
 */
RunStatus run_thread_get_info(RUN_SIZE runIndex, RunThreadInfo *runInfoOut);

/*
 * Scheduler locks nest. Preemption remains inhibited until the matching unlock
 * drops the count to zero, at which point a pending priority change may switch.
 */
RUN_VOID run_scheduler_lock(RUN_VOID);
RUN_VOID run_scheduler_unlock(RUN_VOID);

#endif

