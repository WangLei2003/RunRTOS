/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - architecture port interface
 *
 * Layer: arch
 * Purpose: Isolate scheduler-visible CPU operations from the kernel.
 * Dependencies: core types and compiler adaptation.
 * Constraint: Implementations are architecture-specific; callers must respect
 *             critical-section and interrupt-context rules documented per API.
 */
#ifndef RUN_ARCH_H_
#define RUN_ARCH_H_

#include "runrtos/core/run_types.h"

typedef RUN_UINT_32 RunCriticalState;

/*
 * Architecture context initialization is consumed by the kernel. runEntry runs
 * with runEntryArgument and is expected to terminate through runReturn.
 */
typedef RUN_STRUCT RunArchThreadContextConfig
{
    RUN_VOID *runStackBase;
    RUN_SIZE runStackSize;
    RUN_VOID (*runEntry)(RUN_VOID *runArgument);
    RUN_VOID *runEntryArgument;
    RUN_VOID (*runReturn)(RUN_VOID);
} RunArchThreadContextConfig;

/* Configures fault priorities, PendSV, SysTick, and optional cycle counter. */
RUN_VOID run_arch_early_init(RUN_VOID);
RunStatus run_arch_context_initialize(RUN_CONST RunArchThreadContextConfig *runConfig,
                                      RUN_SIZE *runContextOut);
/* Releases architecture-owned context storage after the thread is unreachable. */
RUN_VOID run_arch_context_destroy(RUN_SIZE runContext);
/* Some ports cannot recycle their currently executing context until after switch. */
RUN_BOOL run_arch_context_requires_deferred_release(RUN_VOID);

/* The returned token must be passed unchanged to the matching exit function. */
RunCriticalState run_arch_enter_critical(RUN_VOID);
RUN_VOID run_arch_exit_critical(RunCriticalState runState);

/* Requests deferred scheduling through PendSV, normally with interrupts masked. */
RUN_VOID run_arch_trigger_context_switch(RUN_VOID);

/* Installs PSP for the first thread and raises PendSV; control leaves the MSP. */
RUN_VOID run_arch_start_scheduler(RUN_VOID);

/* Enters the architecture's lowest-power idle instruction until the next IRQ. */
RUN_VOID run_arch_idle(RUN_VOID);
RUN_UINT_32 run_arch_get_ipsr(RUN_VOID);
RUN_UINT_32 run_arch_get_cycle_count(RUN_VOID);

/* Cortex-M IRQ numbers are exception numbers; vectors 0-15 are not valid here. */
RUN_VOID run_arch_enable_irq(RUN_UINT_32 runIrq);
RUN_VOID run_arch_disable_irq(RUN_UINT_32 runIrq);
RUN_VOID run_arch_set_irq_priority(RUN_UINT_32 runIrq, RUN_UINT_8 runPriority);
RUN_ATTR_NORETURN RUN_VOID run_arch_system_reset(RUN_VOID);

#endif

