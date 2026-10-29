/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - bounded background stress service
 *
 * Layer: components/stress
 * Purpose: Exercise kernel objects and allocator from a dedicated thread.
 * Dependencies: public kernel, synchronization, and runtime interfaces.
 * Constraint: Call start/stop/delete from thread context only. Completion runs
 *             on the worker thread and must not block.
 */
#ifndef RUN_STRESS_H_
#define RUN_STRESS_H_

#include "runrtos/kernel/run_sync.h"

typedef RUN_STRUCT RunStress RunStress;

typedef RUN_STRUCT RunStressInfo
{
    RUN_BOOL runActive;
    RUN_UINT_32 runIterations;
    RUN_UINT_32 runFailures;
    RUN_SIZE runMemoryBaseline;
    RUN_SIZE runMemoryUsed;
    RUN_SIZE runMemoryPeak;
    RUN_UINT_32 runAllocatorFailures;
    RUN_SIZE runStackSize;
    RUN_SIZE runStackHighWater;
} RunStressInfo;

typedef RUN_VOID (*RunStressComplete)(RunStress *runStress,
                                      RUN_CONST RunStressInfo *runInfo,
                                      RUN_VOID *runArgument);

typedef RUN_STRUCT RunStressConfig
{
    RunMemory *runMemory;
    RUN_SIZE runStackSize;
    RUN_UINT_8 runPriority;
    RunStressComplete runComplete;
    RUN_VOID *runCompleteArgument;
} RunStressConfig;

RunStatus run_stress_create(RUN_CONST RunStressConfig *runConfig,
                            RunStress **runStressOut);
/* Starts one run; seconds zero selects the 60-second default. */
RunStatus run_stress_start(RunStress *runStress, RUN_UINT_32 runSeconds);
/* Requests cooperative stop; the completion callback reports the final state. */
RunStatus run_stress_stop(RunStress *runStress);
RUN_VOID run_stress_get_info(RunStress *runStress, RunStressInfo *runInfoOut);
/* Deletes kernel objects; if active, requests stop and waits for worker exit. */
RunStatus run_stress_delete(RunStress *runStress);

#endif
