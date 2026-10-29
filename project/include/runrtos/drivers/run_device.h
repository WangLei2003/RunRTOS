/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - generic device object and capability dispatch
 *
 * Layer: drivers
 * Purpose: Provide a common dynamic device lifecycle and class identity.
 * Dependencies: kernel allocation service.
 * Constraint: Device objects and private contexts are kernel-owned and may only
 *             be created or deleted from thread context with memory initialized.
 */
#ifndef RUN_DEVICE_H_
#define RUN_DEVICE_H_

#include "runrtos/kernel/run_kernel.h"

typedef RUN_STRUCT RunDevice RunDevice;

typedef RUN_ENUM RunDeviceClass
{
    RUN_DEVICE_CLASS_GPIO = 0,
    RUN_DEVICE_CLASS_UART,
    RUN_DEVICE_CLASS_SPI,
    RUN_DEVICE_CLASS_I2C
} RunDeviceClass;

typedef RUN_STRUCT RunDeviceConfig
{
    /* Borrowed descriptive name; the device does not copy it. */
    RUN_CONST RUN_CHAR *runName;
    RunDeviceClass runClass;
    /* Const backend-owned operation table selected by the concrete device. */
    RUN_CONST RUN_VOID *runOperations;
    /* Zero creates a device without a private backend context. */
    RUN_SIZE runContextSize;
} RunDeviceConfig;

/*
 * Create/delete require thread context and an initialized allocator. The context
 * returned by run_device_context is borrowed and becomes invalid on deletion.
 */
RunStatus run_device_create(RUN_CONST RunDeviceConfig *runConfig, RunDevice **runDeviceOut);
RunStatus run_device_delete(RunDevice *runDevice);

/* Read-only accessors for diagnostics; a null device yields null or GPIO class. */
RUN_CONST RUN_CHAR *run_device_name(RunDevice *runDevice);
RunDeviceClass run_device_class(RunDevice *runDevice);
RUN_CONST RUN_VOID *run_device_operations(RunDevice *runDevice);
RUN_VOID *run_device_context(RunDevice *runDevice);

#endif

