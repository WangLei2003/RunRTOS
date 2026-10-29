/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - GPIO capability interface
 *
 * Layer: drivers
 * Purpose: Describe portable pin configuration, I/O, and interrupt capability.
 * Dependencies: generic RunDevice framework.
 * Constraint: Pin numbers are 0..15 within one device/port; ISR attachment is
 *             optional and returns UNSUPPORTED when absent.
 */
#ifndef RUN_GPIO_H_
#define RUN_GPIO_H_

#include "runrtos/drivers/run_device.h"

typedef RUN_VOID (*RunGpioIrqCallback)(RunDevice *runDevice,
                                      RUN_UINT_16 runPin,
                                      RUN_VOID *runArgument);

typedef RUN_ENUM RunGpioMode
{
    RUN_GPIO_MODE_INPUT = 0,
    RUN_GPIO_MODE_OUTPUT_PUSH_PULL,
    RUN_GPIO_MODE_OUTPUT_OPEN_DRAIN,
    RUN_GPIO_MODE_ALTERNATE_PUSH_PULL,
    RUN_GPIO_MODE_ALTERNATE_OPEN_DRAIN,
    RUN_GPIO_MODE_ANALOG
} RunGpioMode;

typedef RUN_ENUM RunGpioPull
{
    RUN_GPIO_PULL_NONE = 0,
    RUN_GPIO_PULL_UP,
    RUN_GPIO_PULL_DOWN
} RunGpioPull;

typedef RUN_STRUCT RunGpioOperations
{
    /* Mode and pull values are RunGpioMode/RunGpioPull stored as portable bytes. */
    RunStatus (*runConfigure)(RunDevice *runDevice,
                              RUN_UINT_16 runPin,
                              RUN_UINT_8 runMode,
                              RUN_UINT_8 runPull);
    RunStatus (*runWrite)(RunDevice *runDevice, RUN_UINT_16 runPin, RUN_BOOL runHigh);
    RunStatus (*runToggle)(RunDevice *runDevice, RUN_UINT_16 runPin);
    RunStatus (*runRead)(RunDevice *runDevice, RUN_UINT_16 runPin, RUN_BOOL *runHighOut);
    /* Optional capability; absent implementations return RUN_STATUS_UNSUPPORTED. */
    RunStatus (*runAttachInterrupt)(RunDevice *runDevice,
                                    RUN_UINT_16 runPin,
                                    RUN_BOOL runRising,
                                    RUN_BOOL runFalling,
                                    RunGpioIrqCallback runCallback,
                                    RUN_VOID *runArgument);
} RunGpioOperations;

RUN_STATIC RUN_INLINE RUN_CONST RunGpioOperations *run_gpio_ops(RunDevice *runDevice)
{
    return (RUN_CONST RunGpioOperations *)run_device_operations(runDevice);
}

/*
 * Thin capability dispatch. These calls validate the device operation table and
 * required output pointers; concrete backends validate pin numbering and values.
 */
RunStatus run_gpio_configure(RunDevice *runDevice,
                             RUN_UINT_16 runPin,
                             RUN_UINT_8 runMode,
                             RUN_UINT_8 runPull);
RunStatus run_gpio_write(RunDevice *runDevice, RUN_UINT_16 runPin, RUN_BOOL runHigh);
RunStatus run_gpio_toggle(RunDevice *runDevice, RUN_UINT_16 runPin);
RunStatus run_gpio_read(RunDevice *runDevice, RUN_UINT_16 runPin, RUN_BOOL *runHighOut);
RunStatus run_gpio_create_f103(RUN_UINT_32 runPortIndex,
                               RUN_CONST RUN_CHAR *runName,
                               RunDevice **runDeviceOut);

#endif

