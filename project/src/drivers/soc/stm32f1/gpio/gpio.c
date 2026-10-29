/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - STM32F1 GPIO backend
 *
 * Layer: drivers/soc/stm32f1
 * Purpose: Translate portable GPIO operations into CRL/CRH/IDR/BSRR/BRR accesses.
 * Dependencies: public GPIO interface, generic device allocation, arch cycle timer.
 * Constraint: Register bases and pin encodings are private to this file.
 */
#include "runrtos/drivers/gpio/run_gpio.h"

#define RUN_STM32F1_GPIO_PIN_COUNT 16U
#define RUN_STM32F1_GPIO_PINS_PER_CONTROL 8U
#define RUN_STM32F1_GPIO_CONFIG_BITS 4U
#define RUN_STM32F1_GPIO_INPUT_FLOATING 0x4U
#define RUN_STM32F1_GPIO_INPUT_PULL 0x8U
#define RUN_STM32F1_GPIO_OUTPUT_PUSH_PULL_2MHZ 0x2U
#define RUN_STM32F1_GPIO_OUTPUT_OPEN_DRAIN_2MHZ 0x6U
#define RUN_STM32F1_GPIO_ALTERNATE_PUSH_PULL_2MHZ 0xAU
#define RUN_STM32F1_GPIO_ALTERNATE_OPEN_DRAIN_2MHZ 0xEU
#define RUN_STM32F1_GPIO_ANALOG 0x0U

typedef RUN_STRUCT RunStm32f1Gpio
{
    RUN_VOLATILE RUN_UINT_32 *runControlLow;
    RUN_VOLATILE RUN_UINT_32 *runControlHigh;
    RUN_VOLATILE RUN_UINT_32 *runInput;
    RUN_VOLATILE RUN_UINT_32 *runOutput;
    RUN_VOLATILE RUN_UINT_32 runBitSetReset;
    RUN_VOLATILE RUN_UINT_32 runReset;
} RunStm32f1Gpio;

typedef RUN_STRUCT RunStm32f1GpioRegisters
{
    RUN_VOLATILE RUN_UINT_32 runControlLow;
    RUN_VOLATILE RUN_UINT_32 runControlHigh;
    RUN_VOLATILE RUN_UINT_32 runInput;
    RUN_VOLATILE RUN_UINT_32 runOutput;
    RUN_VOLATILE RUN_UINT_32 runBitSetReset;
    RUN_VOLATILE RUN_UINT_32 runReset;
    RUN_VOLATILE RUN_UINT_32 runLock;
} RunStm32f1GpioRegisters;

RUN_STATIC RunStatus run_stm32f1_gpio_configure(RunDevice *runDevice,
                                                RUN_UINT_16 runPin,
                                                RUN_UINT_8 runMode,
                                                RUN_UINT_8 runPull)
{
    RunStm32f1Gpio *runGpio = (RunStm32f1Gpio *)run_device_context(runDevice);
    RUN_VOLATILE RUN_UINT_32 *runRegister;
    RUN_UINT_32 runShift;
    RUN_UINT_32 runConfiguration;
    RUN_UINT_32 runMask;

    if ((runGpio == RUN_NULL) || (runPin >= RUN_STM32F1_GPIO_PIN_COUNT))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    /* CRL owns pins 0-7 and CRH owns pins 8-15, four bits per pin. */
    if (runPin < RUN_STM32F1_GPIO_PINS_PER_CONTROL)
    {
        runRegister = runGpio->runControlLow;
        runShift = (RUN_UINT_32)runPin * RUN_STM32F1_GPIO_CONFIG_BITS;
    }
    else
    {
        runRegister = runGpio->runControlHigh;
        runShift = ((RUN_UINT_32)runPin - RUN_STM32F1_GPIO_PINS_PER_CONTROL) *
            RUN_STM32F1_GPIO_CONFIG_BITS;
    }

    switch ((RunGpioMode)runMode)
    {
    case RUN_GPIO_MODE_INPUT:
        runConfiguration = ((RunGpioPull)runPull == RUN_GPIO_PULL_NONE) ?
            RUN_STM32F1_GPIO_INPUT_FLOATING : RUN_STM32F1_GPIO_INPUT_PULL;
        /* F1 selects pull direction through the output latch while in input mode. */
        if ((RunGpioPull)runPull == RUN_GPIO_PULL_DOWN)
        {
            RUN_UINT_32 runPinMask = 1UL << runPin;
            runGpio->runBitSetReset = runPinMask << 16U;
        }
        else if ((RunGpioPull)runPull == RUN_GPIO_PULL_UP)
        {
            runGpio->runBitSetReset = 1UL << runPin;
        }
        break;
    case RUN_GPIO_MODE_OUTPUT_PUSH_PULL:
        runConfiguration = RUN_STM32F1_GPIO_OUTPUT_PUSH_PULL_2MHZ;
        break;
    case RUN_GPIO_MODE_OUTPUT_OPEN_DRAIN:
        runConfiguration = RUN_STM32F1_GPIO_OUTPUT_OPEN_DRAIN_2MHZ;
        break;
    case RUN_GPIO_MODE_ALTERNATE_PUSH_PULL:
        runConfiguration = RUN_STM32F1_GPIO_ALTERNATE_PUSH_PULL_2MHZ;
        break;
    case RUN_GPIO_MODE_ALTERNATE_OPEN_DRAIN:
        runConfiguration = RUN_STM32F1_GPIO_ALTERNATE_OPEN_DRAIN_2MHZ;
        break;
    case RUN_GPIO_MODE_ANALOG:
        runConfiguration = RUN_STM32F1_GPIO_ANALOG;
        break;
    default:
        return RUN_STATUS_INVALID_ARGUMENT;
    }

    runMask = ((1UL << RUN_STM32F1_GPIO_CONFIG_BITS) - 1U) << runShift;
    *runRegister = (*runRegister & ~runMask) | (runConfiguration << runShift);
    return RUN_STATUS_OK;
}

RUN_STATIC RunStatus run_stm32f1_gpio_write(RunDevice *runDevice,
                                            RUN_UINT_16 runPin,
                                            RUN_BOOL runHigh)
{
    RunStm32f1Gpio *runGpio = (RunStm32f1Gpio *)run_device_context(runDevice);
    if ((runGpio == RUN_NULL) || (runPin >= RUN_STM32F1_GPIO_PIN_COUNT))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    /* BSRR/BRR avoid a read-modify-write of ODR and naturally ignore short pulses. */
    if (runHigh)
    {
        runGpio->runBitSetReset = 1UL << runPin;
    }
    else
    {
        runGpio->runReset = 1UL << runPin;
    }
    return RUN_STATUS_OK;
}

RUN_STATIC RunStatus run_stm32f1_gpio_toggle(RunDevice *runDevice, RUN_UINT_16 runPin)
{
    RunStm32f1Gpio *runGpio = (RunStm32f1Gpio *)run_device_context(runDevice);
    RUN_UINT_32 runCurrent;
    if ((runGpio == RUN_NULL) || (runPin >= RUN_STM32F1_GPIO_PIN_COUNT))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    /* Read-modify-write is safe only when this port is not concurrently toggled. */
    runCurrent = *runGpio->runOutput;
    if ((runCurrent & (1UL << runPin)) != 0U)
    {
        runGpio->runReset = 1UL << runPin;
    }
    else
    {
        runGpio->runBitSetReset = 1UL << runPin;
    }
    return RUN_STATUS_OK;
}

RUN_STATIC RunStatus run_stm32f1_gpio_read(RunDevice *runDevice,
                                           RUN_UINT_16 runPin,
                                           RUN_BOOL *runHighOut)
{
    RunStm32f1Gpio *runGpio = (RunStm32f1Gpio *)run_device_context(runDevice);
    if ((runGpio == RUN_NULL) || (runPin >= RUN_STM32F1_GPIO_PIN_COUNT) ||
        (runHighOut == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    *runHighOut = ((*runGpio->runInput & (1UL << runPin)) != 0U) ?
        RUN_TRUE : RUN_FALSE;
    return RUN_STATUS_OK;
}

RUN_STATIC RUN_CONST RunGpioOperations RunStm32f1GpioOperations = {
    run_stm32f1_gpio_configure,
    run_stm32f1_gpio_write,
    run_stm32f1_gpio_toggle,
    run_stm32f1_gpio_read,
    RUN_NULL
};

RunStatus run_gpio_create_f103(RUN_UINT_32 runPortIndex,
                               RUN_CONST RUN_CHAR *runName,
                               RunDevice **runDeviceOut)
{
    /* GPIO A-E register block bases for the STM32F103 high-density package. */
    RUN_CONST RUN_UINTPTR_TYPE runBaseTable[] = {
        0x40010800U, 0x40010C00U, 0x40011000U,
        0x40011400U, 0x40011800U
    };
    RunDeviceConfig runConfig;
    RunStatus runStatus;
    RunStm32f1Gpio *runGpio;
    RunStm32f1GpioRegisters *runRegisters;

    if ((runDeviceOut == RUN_NULL) || (runPortIndex >= RUN_ARRAY_COUNT(runBaseTable)))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runConfig.runName = runName;
    runConfig.runClass = RUN_DEVICE_CLASS_GPIO;
    runConfig.runOperations = &RunStm32f1GpioOperations;
    runConfig.runContextSize = sizeof(RunStm32f1Gpio);
    runStatus = run_device_create(&runConfig, runDeviceOut);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runGpio = (RunStm32f1Gpio *)run_device_context(*runDeviceOut);
    runRegisters = (RunStm32f1GpioRegisters *)(RUN_VOID *)runBaseTable[runPortIndex];
    runGpio->runControlLow = &runRegisters->runControlLow;
    runGpio->runControlHigh = &runRegisters->runControlHigh;
    runGpio->runInput = &runRegisters->runInput;
    runGpio->runOutput = &runRegisters->runOutput;
    runGpio->runBitSetReset = runRegisters->runBitSetReset;
    runGpio->runReset = runRegisters->runReset;
    return RUN_STATUS_OK;
}

