/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - GPIO capability forwarding
 *
 * Layer: drivers
 * Purpose: Validate common arguments before dispatching to a GPIO operation table.
 * Dependencies: public GPIO/device interfaces.
 * Constraint: Missing backend capabilities map to RUN_STATUS_UNSUPPORTED.
 */
#include "runrtos/drivers/gpio/run_gpio.h"

RunStatus run_gpio_configure(RunDevice *runDevice,
                             RUN_UINT_16 runPin,
                             RUN_UINT_8 runMode,
                             RUN_UINT_8 runPull)
{
    RUN_CONST RunGpioOperations *runOperations = run_gpio_ops(runDevice);
    if ((runOperations == RUN_NULL) || (runOperations->runConfigure == RUN_NULL))
    {
        return RUN_STATUS_UNSUPPORTED;
    }
    return runOperations->runConfigure(runDevice, runPin, runMode, runPull);
}

RunStatus run_gpio_write(RunDevice *runDevice, RUN_UINT_16 runPin, RUN_BOOL runHigh)
{
    RUN_CONST RunGpioOperations *runOperations = run_gpio_ops(runDevice);
    if ((runOperations == RUN_NULL) || (runOperations->runWrite == RUN_NULL))
    {
        return RUN_STATUS_UNSUPPORTED;
    }
    return runOperations->runWrite(runDevice, runPin, runHigh);
}

RunStatus run_gpio_toggle(RunDevice *runDevice, RUN_UINT_16 runPin)
{
    RUN_CONST RunGpioOperations *runOperations = run_gpio_ops(runDevice);
    if ((runOperations == RUN_NULL) || (runOperations->runToggle == RUN_NULL))
    {
        return RUN_STATUS_UNSUPPORTED;
    }
    return runOperations->runToggle(runDevice, runPin);
}

RunStatus run_gpio_read(RunDevice *runDevice, RUN_UINT_16 runPin, RUN_BOOL *runHighOut)
{
    RUN_CONST RunGpioOperations *runOperations = run_gpio_ops(runDevice);
    if ((runOperations == RUN_NULL) || (runOperations->runRead == RUN_NULL) ||
        (runHighOut == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    return runOperations->runRead(runDevice, runPin, runHighOut);
}

