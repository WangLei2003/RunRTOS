/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - dynamic device object lifecycle
 *
 * Layer: drivers
 * Purpose: Allocate, register, inspect, and release generic devices.
 * Dependencies: kernel allocator and public device contract.
 * Constraint: A device and its private context are released together; delete is
 *             forbidden while another thread still holds the opaque handle.
 */
#include "../../kernel/internal/run_kernel_internal.h"
#include "runrtos/drivers/run_device.h"

#define RUN_DEVICE_MAGIC 0x52554E44U

RUN_STRUCT RunDevice
{
    RUN_UINT_32 runMagic;
    RUN_CONST RUN_CHAR *runName;
    RunDeviceClass runClass;
    RUN_CONST RUN_VOID *runOperations;
    RUN_VOID *runContext;
    RUN_SIZE runContextSize;
    RunDevice *runNext;
};

RUN_STATIC RunDevice *RunDeviceFirst;

/* Allocates a generic device and its private backend context. */
RunStatus run_device_create(RUN_CONST RunDeviceConfig *runConfig, RunDevice **runDeviceOut)
{
    RunDevice *runDevice;
    RunStatus runStatus;
    RunCriticalState runState;
    if ((runConfig == RUN_NULL) || (runDeviceOut == RUN_NULL) ||
        (runConfig->runOperations == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runStatus = run_kernel_api_enter(RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runState = run_arch_enter_critical();
    runStatus = run_memory_allocate(RunKernel.runMemory, sizeof(RunDevice),
                                    RUN_MEMORY_ALIGNMENT, (RUN_VOID **)&runDevice);
    if (runStatus == RUN_STATUS_OK)
    {
        run_memory_set(runDevice, 0, sizeof(RunDevice));
    }
    if ((runStatus == RUN_STATUS_OK) && (runConfig->runContextSize != 0U))
    {
        runStatus = run_memory_allocate(RunKernel.runMemory,
                                        runConfig->runContextSize,
                                        RUN_MEMORY_ALIGNMENT,
                                        &runDevice->runContext);
        if (runStatus == RUN_STATUS_OK)
        {
            run_memory_set(runDevice->runContext, 0, runConfig->runContextSize);
        }
        else
        {
            (RUN_VOID)run_memory_release(RunKernel.runMemory, runDevice);
        }
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runDevice->runMagic = RUN_DEVICE_MAGIC;
        runDevice->runName = runConfig->runName;
        runDevice->runClass = runConfig->runClass;
        runDevice->runOperations = runConfig->runOperations;
        runDevice->runContextSize = runConfig->runContextSize;
        runDevice->runNext = RunDeviceFirst;
        RunDeviceFirst = runDevice;
        *runDeviceOut = runDevice;
    }
    run_arch_exit_critical(runState);
    return runStatus;
}

/* Unlinks and releases a device together with its backend context. */
RunStatus run_device_delete(RunDevice *runDevice)
{
    RunDevice *runCurrent = RunDeviceFirst;
    RunDevice *runPrevious = RUN_NULL;
    RunCriticalState runState;
    if ((runDevice == RUN_NULL) || (runDevice->runMagic != RUN_DEVICE_MAGIC))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runState = run_arch_enter_critical();
    while (runCurrent != RUN_NULL)
    {
        if (runCurrent == runDevice)
        {
            if (runPrevious == RUN_NULL)
            {
                RunDeviceFirst = runCurrent->runNext;
            }
            else
            {
                runPrevious->runNext = runCurrent->runNext;
            }
            runCurrent->runMagic = 0U;
            if (runCurrent->runContext != RUN_NULL)
            {
                (RUN_VOID)run_memory_release(RunKernel.runMemory,
                                             runCurrent->runContext);
            }
            (RUN_VOID)run_memory_release(RunKernel.runMemory, runCurrent);
            run_arch_exit_critical(runState);
            return RUN_STATUS_OK;
        }
        runPrevious = runCurrent;
        runCurrent = runCurrent->runNext;
    }
    run_arch_exit_critical(runState);
    return RUN_STATUS_NOT_FOUND;
}

/* Returns the read-only device name, or NULL for an invalid handle. */
RUN_CONST RUN_CHAR *run_device_name(RunDevice *runDevice)
{
    return (runDevice != RUN_NULL) ? runDevice->runName : RUN_NULL;
}

/* Returns the device capability class, defaulting safely for a null handle. */
RunDeviceClass run_device_class(RunDevice *runDevice)
{
    return (runDevice != RUN_NULL) ? runDevice->runClass : RUN_DEVICE_CLASS_GPIO;
}

/* Exposes the immutable backend operation table for capability dispatch. */
RUN_CONST RUN_VOID *run_device_operations(RunDevice *runDevice)
{
    return (runDevice != RUN_NULL) ? runDevice->runOperations : RUN_NULL;
}

/* Returns the backend-owned context without exposing its concrete type. */
RUN_VOID *run_device_context(RunDevice *runDevice)
{
    return (runDevice != RUN_NULL) ? runDevice->runContext : RUN_NULL;
}

