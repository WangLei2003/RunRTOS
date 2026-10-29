/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - private scheduler and thread representation
 *
 * Layer: kernel (private)
 * Purpose: Share invariants and wait-list operations inside the kernel/port.
 * Dependencies: public core, arch, and kernel contracts.
 * Constraint: Not installed publicly. RunThread offset zero is an assembler ABI
 *             contract and must remain the saved process stack pointer.
 */
#ifndef RUN_KERNEL_INTERNAL_H_
#define RUN_KERNEL_INTERNAL_H_

#include "runrtos/arch/run_arch.h"
#include "runrtos/core/run_assert.h"
#include "runrtos/core/run_config.h"
#include "runrtos/kernel/run_kernel.h"

typedef RUN_ENUM RunThreadState
{
    RUN_THREAD_CREATED = 0,
    RUN_THREAD_READY,
    RUN_THREAD_RUNNING,
    RUN_THREAD_BLOCKED,
    RUN_THREAD_SUSPENDED,
    RUN_THREAD_TERMINATED
} RunThreadState;

typedef RUN_STRUCT RunWaitList
{
    RunThread *runFirst;
    RunThread *runLast;
} RunWaitList;

typedef RUN_STRUCT RunMutex RunMutex;

RUN_STRUCT RunThread
{
    /* PendSV assembly stores the process stack pointer at offset zero. */
    RUN_VOLATILE RUN_SIZE runStackPointer;
    RunThread *runReadyNext;
    RunThread *runWaitNext;
    RunThread *runWaitPrevious;
    RunWaitList *runWaitList;
    RunThread *runAllNext;
    RunThread *runTimeoutNext;
    RunMemory *runMemory;
    RUN_UINT_8 *runStackBase;
    RUN_SIZE runStackSize;
    RunThreadEntry runEntry;
    RUN_VOID *runArgument;
    RUN_CONST RUN_CHAR *runName;
    RUN_UINT_8 runBasePriority;
    RUN_UINT_8 runPriority;
    RUN_UINT_32 runTimeSlice;
    RUN_UINT_32 runTimeSliceRemaining;
    RunTick runTimeoutAt;
    RUN_UINT_32 runWakeStatus;
    RUN_UINT_64 runContextSwitches;
    RunMutex *runHeldMutexes;
    RunMutex *runNextHeldMutex;
    RunThreadState runState;
};

typedef RUN_STRUCT RunKernelState
{
    RunMemory *runMemory;
    RunThread *runCurrent;
    RunThread *runIdle;
    RunThread *runThreads;
    RunThread *runReady[RUN_CONFIG_PRIORITY_COUNT];
    RunThread *runReadyLast[RUN_CONFIG_PRIORITY_COUNT];
    RUN_UINT_32 runReadyBitmap;
    RunThread *runTimeoutFirst;
    RUN_UINT_64 runTicks;
    RUN_UINT_64 runContextSwitches;
    RUN_UINT_32 runThreadCount;
    RUN_UINT_32 runSchedulerLock;
    RUN_BOOL runInitialized;
    RUN_BOOL runRunning;
    RUN_BOOL runSwitchRequested;
} RunKernelState;

RUN_EXTERN RunKernelState RunKernel;
RUN_EXTERN RunThread *RUN_VOLATILE RunKernelCurrent;

RunStatus run_kernel_api_enter(RUN_BOOL runAllowIsr);
RunStatus run_kernel_block_current(RunTimeMs runTimeout,
                                   RunWaitList *runWaitList,
                                   RUN_UINT_32 *runWakeStatusOut);
RUN_VOID run_kernel_ready_thread(RunThread *runThread, RunStatus runWakeStatus);
RUN_VOID run_kernel_wait_list_append(RunWaitList *runWaitList, RunThread *runThread);
RUN_VOID run_kernel_wait_list_remove(RunThread *runThread);
RunThread *run_kernel_wait_list_pop(RunWaitList *runWaitList);
RUN_VOID run_kernel_request_switch(RUN_VOID);
RUN_BOOL run_kernel_validate_thread(RunThread *runThread);
RUN_VOID run_kernel_idle_entry(RUN_VOID *runArgument);
RunStatus run_kernel_create_internal(RUN_CONST RunThreadConfig *runConfig,
                                     RunThread **runThreadOut);

#endif

