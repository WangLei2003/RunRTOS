/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - STM32F1 polling I2C1 backend
 *
 * Layer: drivers/soc/stm32f1
 * Purpose: Perform bounded 7-bit master writes and reads through I2C1.
 * Dependencies: public I2C interface and Cortex-M DWT cycle counter.
 * Constraint: Board code owns PB6/PB7 open-drain setup; combined write-then-read
 *             transactions and asynchronous completion are not yet implemented.
 */
#include "runrtos/arch/run_arch.h"
#include "runrtos/drivers/i2c/run_i2c.h"

#define RUN_STM32F1_I2C1_BASE 0x40005400U
#define RUN_STM32F1_I2C_CR1_SWRST (1UL << 15U)
#define RUN_STM32F1_I2C_CR1_ACK (1UL << 10U)
#define RUN_STM32F1_I2C_CR1_STOP (1UL << 9U)
#define RUN_STM32F1_I2C_CR1_START (1UL << 8U)
#define RUN_STM32F1_I2C_CR1_PE (1UL << 0U)
#define RUN_STM32F1_I2C_SR1_SB (1UL << 0U)
#define RUN_STM32F1_I2C_SR1_ADDR (1UL << 1U)
#define RUN_STM32F1_I2C_SR1_BTF (1UL << 2U)
#define RUN_STM32F1_I2C_SR1_RXNE (1UL << 6U)
#define RUN_STM32F1_I2C_SR1_TXE (1UL << 7U)
#define RUN_STM32F1_I2C_SR2_BUSY (1UL << 1U)
#define RUN_STM32F1_I2C_TIMEOUT_CYCLES 720000U
#define RUN_STM32F1_I2C_CCR_100KHZ 90U
#define RUN_STM32F1_I2C_TRISE_100KHZ 37U

typedef RUN_STRUCT RunStm32f1I2cRegisters
{
    RUN_VOLATILE RUN_UINT_32 runControl1;
    RUN_VOLATILE RUN_UINT_32 runControl2;
    RUN_VOLATILE RUN_UINT_32 runOwnAddress1;
    RUN_VOLATILE RUN_UINT_32 runOwnAddress2;
    RUN_VOLATILE RUN_UINT_32 runData;
    RUN_VOLATILE RUN_UINT_32 runStatus1;
    RUN_VOLATILE RUN_UINT_32 runStatus2;
    RUN_VOLATILE RUN_UINT_32 runClockControl;
    RUN_VOLATILE RUN_UINT_32 runTrise;
} RunStm32f1I2cRegisters;

typedef RUN_STRUCT RunStm32f1I2c
{
    RunStm32f1I2cRegisters *runRegisters;
    RUN_BOOL runInitialized;
} RunStm32f1I2c;

RUN_STATIC RUN_BOOL run_stm32f1_i2c_timed_out(RUN_UINT_32 runStart,
                                              RUN_UINT_32 runTimeoutMs)
{
    /* Zero selects a one-ms fallback; DWT wraps safely via unsigned subtraction. */
    RUN_UINT_32 runLimit = (runTimeoutMs == 0U) ?
        RUN_STM32F1_I2C_TIMEOUT_CYCLES :
        runTimeoutMs * (72000000U / 1000U);
    return ((run_arch_get_cycle_count() - runStart) > runLimit) ?
        RUN_TRUE : RUN_FALSE;
}

RUN_STATIC RUN_VOID run_stm32f1_i2c_initialize(RunStm32f1I2c *runI2c)
{
    if (!runI2c->runInitialized)
    {
        runI2c->runRegisters->runControl1 = RUN_STM32F1_I2C_CR1_SWRST;
        runI2c->runRegisters->runControl1 = 0U;
        /* Preset values target 100 kHz at the board's fixed 36 MHz PCLK1. */
        runI2c->runRegisters->runClockControl = RUN_STM32F1_I2C_CCR_100KHZ;
        runI2c->runRegisters->runTrise = RUN_STM32F1_I2C_TRISE_100KHZ;
        runI2c->runRegisters->runControl1 =
            RUN_STM32F1_I2C_CR1_PE | RUN_STM32F1_I2C_CR1_ACK;
        runI2c->runInitialized = RUN_TRUE;
    }
}

RUN_STATIC RunStatus run_stm32f1_i2c_wait_set(RunStm32f1I2c *runI2c,
                                              RUN_UINT_32 runBit,
                                              RUN_UINT_32 runStart,
                                              RUN_UINT_32 runTimeoutMs)
{
    while ((runI2c->runRegisters->runStatus1 & runBit) == 0U)
    {
        if (run_stm32f1_i2c_timed_out(runStart, runTimeoutMs))
        {
            runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_STOP;
            return RUN_STATUS_TIMEOUT;
        }
    }
    return RUN_STATUS_OK;
}

RUN_STATIC RunStatus run_stm32f1_i2c_transaction(RunDevice *runDevice,
                                                 RUN_UINT_16 runAddress,
                                                 RUN_CONST RUN_UINT_8 *runWriteData,
                                                 RUN_SIZE runWriteLength,
                                                 RUN_UINT_8 *runReadData,
                                                 RUN_SIZE runReadLength,
                                                 RUN_UINT_32 runTimeoutMs)
{
    RunStm32f1I2c *runI2c = (RunStm32f1I2c *)run_device_context(runDevice);
    RUN_UINT_32 runStart;
    RunStatus runStatus;
    if ((runI2c == RUN_NULL) || (runAddress > 0x7FU) ||
        ((runWriteLength != 0U) && (runReadLength != 0U)))
    {
        /* Repeated-start write/read needs a separate backend state machine. */
        return RUN_STATUS_UNSUPPORTED;
    }
    run_stm32f1_i2c_initialize(runI2c);
    while ((runI2c->runRegisters->runStatus2 & RUN_STM32F1_I2C_SR2_BUSY) != 0U)
    {
        if (run_stm32f1_i2c_timed_out(run_arch_get_cycle_count(), runTimeoutMs))
        {
            return RUN_STATUS_BUSY;
        }
    }
    runStart = run_arch_get_cycle_count();
    runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_START;
    runStatus = run_stm32f1_i2c_wait_set(runI2c, RUN_STM32F1_I2C_SR1_SB,
                                         runStart, runTimeoutMs);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runI2c->runRegisters->runData =
        ((RUN_UINT_32)runAddress << 1U) |
        ((runWriteLength != 0U) ? 0U : 1U);
    runStatus = run_stm32f1_i2c_wait_set(runI2c, RUN_STM32F1_I2C_SR1_ADDR,
                                         runStart, runTimeoutMs);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }

    if (runWriteLength != 0U)
    {
        /* Reading SR1 then SR2 acknowledges ADDR and releases SCL. */
        (RUN_VOID)runI2c->runRegisters->runStatus1;
        (RUN_VOID)runI2c->runRegisters->runStatus2;
        for (RUN_SIZE runIndex = 0U; runIndex < runWriteLength; runIndex++)
        {
            runStatus = run_stm32f1_i2c_wait_set(runI2c, RUN_STM32F1_I2C_SR1_TXE,
                                                 runStart, runTimeoutMs);
            if (runStatus != RUN_STATUS_OK)
            {
                return runStatus;
            }
            runI2c->runRegisters->runData = runWriteData[runIndex];
        }
        runStatus = run_stm32f1_i2c_wait_set(runI2c, RUN_STM32F1_I2C_SR1_BTF,
                                             runStart, runTimeoutMs);
        if (runStatus != RUN_STATUS_OK)
        {
            return runStatus;
        }
        /* Wait for byte transfer completion before generating STOP. */
        runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_STOP;
        return RUN_STATUS_OK;
    }

    /* ADDR is cleared by the SR1/SR2 read sequence, not by an explicit bit. */
    (RUN_VOID)runI2c->runRegisters->runStatus1;
    (RUN_VOID)runI2c->runRegisters->runStatus2;
    for (RUN_SIZE runIndex = 0U; runIndex < runReadLength; runIndex++)
    {
        if ((runIndex + 1U) == runReadLength)
        {
            /* NACK and request STOP before reading the final byte. */
            runI2c->runRegisters->runControl1 &= ~RUN_STM32F1_I2C_CR1_ACK;
            runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_STOP;
        }
        runStatus = run_stm32f1_i2c_wait_set(runI2c, RUN_STM32F1_I2C_SR1_RXNE,
                                             runStart, runTimeoutMs);
        if (runStatus != RUN_STATUS_OK)
        {
            return runStatus;
        }
        runReadData[runIndex] = (RUN_UINT_8)runI2c->runRegisters->runData;
    }
    /* Leave the controller ready for the next transaction. */
    runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_ACK;
    return RUN_STATUS_OK;
}

RUN_STATIC RunStatus run_stm32f1_i2c_transaction_async(RunDevice *runDevice,
                                                       RUN_UINT_16 runAddress,
                                                       RUN_CONST RUN_UINT_8 *runWriteData,
                                                       RUN_SIZE runWriteLength,
                                                       RUN_UINT_8 *runReadData,
                                                       RUN_SIZE runReadLength,
                                                       RunI2cCompletion runCompletion,
                                                       RUN_VOID *runArgument)
{
    (RUN_VOID)runDevice;
    (RUN_VOID)runAddress;
    (RUN_VOID)runWriteData;
    (RUN_VOID)runWriteLength;
    (RUN_VOID)runReadData;
    (RUN_VOID)runReadLength;
    (RUN_VOID)runCompletion;
    (RUN_VOID)runArgument;
    return RUN_STATUS_UNSUPPORTED;
}

RUN_STATIC RUN_CONST RunI2cOperations RunStm32f1I2cOperations = {
    run_stm32f1_i2c_transaction,
    run_stm32f1_i2c_transaction_async
};

RunStatus run_i2c_create_f103(RUN_UINT_32 runIndex,
                              RUN_CONST RUN_CHAR *runName,
                              RunDevice **runDeviceOut)
{
    RunDeviceConfig runConfig;
    RunStatus runStatus;
    RunStm32f1I2c *runI2c;
    if ((runDeviceOut == RUN_NULL) || (runIndex != 1U))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runConfig.runName = runName;
    runConfig.runClass = RUN_DEVICE_CLASS_I2C;
    runConfig.runOperations = &RunStm32f1I2cOperations;
    runConfig.runContextSize = sizeof(RunStm32f1I2c);
    runStatus = run_device_create(&runConfig, runDeviceOut);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runI2c = (RunStm32f1I2c *)run_device_context(*runDeviceOut);
    runI2c->runRegisters =
        (RunStm32f1I2cRegisters *)(RUN_VOID *)RUN_STM32F1_I2C1_BASE;
    return RUN_STATUS_OK;
}

