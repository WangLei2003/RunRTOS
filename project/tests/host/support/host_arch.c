/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - Windows host-test architecture port
 *
 * Layer: tests/host/support
 * Purpose: Run the production scheduler on Windows fibers for deterministic tests.
 * Dependencies: kernel-private types and Windows fiber/process APIs.
 * Constraint: Test-only; this file is never linked into firmware, and the host
 *             critical section relies on cooperative single-threaded execution.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "run_kernel_internal.h"

typedef RUN_STRUCT RunHostFiberStart
{
    RUN_VOID (*runEntry)(RUN_VOID *runArgument);
    RUN_VOID *runArgument;
} RunHostFiberStart;

RUN_STATIC LPVOID RunHostMainFiber;
RUN_STATIC RUN_BOOL RunHostFiberConverted;

/* Performs the host port's no-op early architecture initialization. */
RUN_VOID run_arch_early_init(RUN_VOID)
{
}

/* Releases fiber bootstrap state and invokes the production thread entry. */
RUN_STATIC RUN_VOID WINAPI run_host_fiber_entry(LPVOID runParameter)
{
    RunHostFiberStart runStart = *(RunHostFiberStart *)runParameter;
    HeapFree(GetProcessHeap(), 0U, runParameter);
    runStart.runEntry(runStart.runArgument);
}

/* Creates a host fiber that represents one scheduler thread context. */
RunStatus run_arch_context_initialize(RUN_CONST RunArchThreadContextConfig *runConfig,
                                      RUN_SIZE *runContextOut)
{
    RunHostFiberStart *runStart;
    LPVOID runFiber;
    if ((runConfig == RUN_NULL) || (runContextOut == RUN_NULL) ||
        (runConfig->runEntry == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    if (!RunHostFiberConverted)
    {
        RunHostMainFiber = ConvertThreadToFiber(RUN_NULL);
        if (RunHostMainFiber == RUN_NULL)
        {
            return RUN_STATUS_ERROR;
        }
        RunHostFiberConverted = RUN_TRUE;
    }
    runStart = (RunHostFiberStart *)HeapAlloc(GetProcessHeap(), 0U,
                                              sizeof(RunHostFiberStart));
    if (runStart == RUN_NULL)
    {
        return RUN_STATUS_NO_MEMORY;
    }
    runStart->runEntry = runConfig->runEntry;
    runStart->runArgument = runConfig->runEntryArgument;
    /*
     * The kernel's accounting stack is intentionally small for MCU targets.
     * Windows fibers need a larger host-only reserve, so keep the accounting
     * size untouched and give the host scheduler a deterministic fixed reserve.
     */
    runFiber = CreateFiber(65536U,
                           run_host_fiber_entry, runStart);
    if (runFiber == RUN_NULL)
    {
        (RUN_VOID)HeapFree(GetProcessHeap(), 0U, runStart);
        return RUN_STATUS_NO_MEMORY;
    }
    *runContextOut = (RUN_SIZE)(RUN_UINTPTR_TYPE)runFiber;
    return RUN_STATUS_OK;
}

/* Deletes a non-current host fiber after the kernel releases its thread. */
RUN_VOID run_arch_context_destroy(RUN_SIZE runContext)
{
    LPVOID runFiber = (LPVOID)(RUN_UINTPTR_TYPE)runContext;
    if ((runFiber != RUN_NULL) && (runFiber != RunHostMainFiber) &&
        (runFiber != GetCurrentFiber()))
    {
        DeleteFiber(runFiber);
    }
}

/* Requires self-deleting fibers to defer cleanup until after switching away. */
RUN_BOOL run_arch_context_requires_deferred_release(RUN_VOID)
{
    return RUN_TRUE;
}

/* Enters the cooperative host port's no-op critical section. */
RunCriticalState run_arch_enter_critical(RUN_VOID)
{
    return 0U;
}

/* Exits the cooperative host port's no-op critical section. */
RUN_VOID run_arch_exit_critical(RunCriticalState runState)
{
    (RUN_VOID)runState;
}

/* Selects the next thread and transfers control through Windows fibers. */
RUN_VOID run_arch_trigger_context_switch(RUN_VOID)
{
    run_kernel_switch_prepare();
    if (RunKernelCurrent != RUN_NULL)
    {
        (RUN_VOID)SwitchToFiber(
            (LPVOID)(RUN_UINTPTR_TYPE)RunKernelCurrent->runContext);
    }
}

/* Transfers the host main fiber into the first scheduled thread. */
RUN_VOID run_arch_start_scheduler(RUN_VOID)
{
    (RUN_VOID)SwitchToFiber(
        (LPVOID)(RUN_UINTPTR_TYPE)RunKernelCurrent->runContext);
}

/* Returns to the host main fiber instead of busy-waiting on an MCU. */
RUN_VOID run_arch_idle(RUN_VOID)
{
    (RUN_VOID)SwitchToFiber(RunHostMainFiber);
}

/* Reports thread mode because Windows fibers have no Cortex-M IPSR. */
RUN_UINT_32 run_arch_get_ipsr(RUN_VOID)
{
    return 0U;
}

/* Returns the monotonic host tick count as the port's cycle counter. */
RUN_UINT_32 run_arch_get_cycle_count(RUN_VOID)
{
    return (RUN_UINT_32)GetTickCount64();
}

/* Ignores host IRQ enable requests because fibers are cooperative. */
RUN_VOID run_arch_enable_irq(RUN_UINT_32 runIrq)
{
    (RUN_VOID)runIrq;
}

/* Ignores host IRQ disable requests because fibers are cooperative. */
RUN_VOID run_arch_disable_irq(RUN_UINT_32 runIrq)
{
    (RUN_VOID)runIrq;
}

/* Ignores host NVIC priority because there is no interrupt controller. */
RUN_VOID run_arch_set_irq_priority(RUN_UINT_32 runIrq, RUN_UINT_8 runPriority)
{
    (RUN_VOID)runIrq;
    (RUN_VOID)runPriority;
}

/* Terminates the host test process for fatal reset requests. */
RUN_ATTR_NORETURN RUN_VOID run_arch_system_reset(RUN_VOID)
{
    ExitProcess(0U);
    for (;;)
    {
    }
}

