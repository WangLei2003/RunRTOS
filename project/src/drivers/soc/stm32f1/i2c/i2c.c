/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - interrupt-driven STM32F1 I2C1 backend
 *
 * Layer: drivers/soc/stm32f1
 * Purpose: Execute 7-bit write, read, and repeated-start write/read transactions.
 * Dependencies: public I2C contract, architecture IRQ controls, and semaphores.
 * Constraint: One transaction may own I2C1; callbacks run in event/error ISRs and
 *             never block. Buffers stay valid until the completion callback.
 */
#include "runrtos/arch/run_arch.h"
#include "runrtos/drivers/i2c/run_i2c.h"
#include "runrtos/kernel/run_sync.h"

#define RUN_STM32F1_I2C1_BASE 0x40005400U
#define RUN_STM32F1_I2C_EVENT_IRQ 31U
#define RUN_STM32F1_I2C_ERROR_IRQ 32U
#define RUN_STM32F1_I2C_IRQ_PRIORITY 0x80U
#define RUN_STM32F1_I2C_CR1_PE (1UL << 0U)
#define RUN_STM32F1_I2C_CR1_START (1UL << 8U)
#define RUN_STM32F1_I2C_CR1_STOP (1UL << 9U)
#define RUN_STM32F1_I2C_CR1_ACK (1UL << 10U)
#define RUN_STM32F1_I2C_CR1_SWRST (1UL << 15U)
#define RUN_STM32F1_I2C_CR2_FREQ_36MHZ 36U
#define RUN_STM32F1_I2C_CR2_ITERREN (1UL << 8U)
#define RUN_STM32F1_I2C_CR2_ITEVTEN (1UL << 9U)
#define RUN_STM32F1_I2C_CR2_ITBUFEN (1UL << 10U)
#define RUN_STM32F1_I2C_SR1_SB (1UL << 0U)
#define RUN_STM32F1_I2C_SR1_ADDR (1UL << 1U)
#define RUN_STM32F1_I2C_SR1_BTF (1UL << 2U)
#define RUN_STM32F1_I2C_SR1_RXNE (1UL << 6U)
#define RUN_STM32F1_I2C_SR1_TXE (1UL << 7U)
#define RUN_STM32F1_I2C_SR1_BERR (1UL << 8U)
#define RUN_STM32F1_I2C_SR1_ARLO (1UL << 9U)
#define RUN_STM32F1_I2C_SR1_AF (1UL << 10U)
#define RUN_STM32F1_I2C_SR1_OVR (1UL << 11U)
#define RUN_STM32F1_I2C_SR1_PECERR (1UL << 12U)
#define RUN_STM32F1_I2C_SR1_TIMEOUT (1UL << 14U)
#define RUN_STM32F1_I2C_SR1_SMBALERT (1UL << 15U)
#define RUN_STM32F1_I2C_ERRORS \
    (RUN_STM32F1_I2C_SR1_BERR | RUN_STM32F1_I2C_SR1_ARLO | \
     RUN_STM32F1_I2C_SR1_AF | RUN_STM32F1_I2C_SR1_OVR | \
     RUN_STM32F1_I2C_SR1_PECERR | RUN_STM32F1_I2C_SR1_TIMEOUT | \
     RUN_STM32F1_I2C_SR1_SMBALERT)
#define RUN_STM32F1_I2C_SR2_BUSY (1UL << 1U)
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

typedef RUN_ENUM RunStm32f1I2cPhase
{
    RUN_STM32F1_I2C_PHASE_WRITE = 0,
    RUN_STM32F1_I2C_PHASE_READ
} RunStm32f1I2cPhase;

typedef RUN_STRUCT RunStm32f1I2c
{
    RunStm32f1I2cRegisters *runRegisters;
    RunSemaphore *runTransactionDone;
    RUN_UINT_16 runAddress;
    RUN_CONST RUN_UINT_8 *runWriteData;
    RUN_SIZE runWriteLength;
    RUN_SIZE runWriteIndex;
    RUN_UINT_8 *runReadData;
    RUN_SIZE runReadLength;
    RUN_SIZE runReadIndex;
    RunStm32f1I2cPhase runPhase;
    RunI2cCompletion runCompletion;
    RUN_VOID *runCompletionArgument;
    RunStatus runCompletionStatus;
    RUN_VOLATILE RUN_BOOL runBusy;
    RUN_BOOL runInitialized;
} RunStm32f1I2c;

RUN_STATIC RunStm32f1I2c *RunStm32f1I2cInstance;
RUN_STATIC RunDevice *RunStm32f1I2cDevice;

/* Resets and configures I2C1 once for 100 kHz operation at 36 MHz. */
RUN_STATIC RUN_VOID run_stm32f1_i2c_initialize(RunStm32f1I2c *runI2c)
{
    if (!runI2c->runInitialized)
    {
        runI2c->runRegisters->runControl1 = RUN_STM32F1_I2C_CR1_SWRST;
        runI2c->runRegisters->runControl1 = 0U;
        runI2c->runRegisters->runControl2 = RUN_STM32F1_I2C_CR2_FREQ_36MHZ;
        runI2c->runRegisters->runClockControl = RUN_STM32F1_I2C_CCR_100KHZ;
        runI2c->runRegisters->runTrise = RUN_STM32F1_I2C_TRISE_100KHZ;
        runI2c->runRegisters->runControl1 =
            RUN_STM32F1_I2C_CR1_PE | RUN_STM32F1_I2C_CR1_ACK;
        runI2c->runInitialized = RUN_TRUE;
    }
}

/* Clears selected I2C error flags using the required SR1 write-zero sequence. */
RUN_STATIC RUN_VOID run_stm32f1_i2c_clear_error_flags(RunStm32f1I2c *runI2c,
                                                     RUN_UINT_32 runErrors)
{
    RUN_UINT_32 runStatus = runI2c->runRegisters->runStatus1;
    /* AF, OVR, BERR, ARLO, and PECERR require writing zero to their SR1 bits. */
    runStatus &= ~runErrors;
    runI2c->runRegisters->runStatus1 = runStatus;
}

/* Releases transaction ownership and signals its ISR or async waiter. */
RUN_STATIC RUN_VOID run_stm32f1_i2c_complete(RunStm32f1I2c *runI2c,
                                             RunStatus runStatus)
{
    RunI2cCompletion runCompletion = runI2c->runCompletion;
    RUN_VOID *runArgument = runI2c->runCompletionArgument;

    runI2c->runRegisters->runControl2 = RUN_STM32F1_I2C_CR2_FREQ_36MHZ;
    runI2c->runCompletionStatus = runStatus;
    runI2c->runBusy = RUN_FALSE;
    runI2c->runWriteData = RUN_NULL;
    runI2c->runWriteLength = 0U;
    runI2c->runReadData = RUN_NULL;
    runI2c->runReadLength = 0U;
    runI2c->runCompletion = RUN_NULL;
    runI2c->runCompletionArgument = RUN_NULL;
    if (runCompletion != RUN_NULL)
    {
        /* Clear ownership before invoking user code in ISR context. */
        runCompletion(RunStm32f1I2cDevice, runStatus, runArgument);
    }
    else
    {
        (RUN_VOID)run_semaphore_give_from_isr(runI2c->runTransactionDone);
    }
}

/* Stops a failed I2C transfer, clears its error cause, and completes it. */
RUN_STATIC RUN_VOID run_stm32f1_i2c_fail(RunStm32f1I2c *runI2c,
                                        RUN_UINT_32 runErrors)
{
    runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_STOP;
    run_stm32f1_i2c_clear_error_flags(runI2c, runErrors);
    run_stm32f1_i2c_complete(runI2c, RUN_STATUS_ERROR);
}

/* Advances the I2C state machine for start, address, byte, and stop events. */
RUN_STATIC RUN_VOID run_stm32f1_i2c_event(RunStm32f1I2c *runI2c)
{
    RUN_UINT_32 runStatus = runI2c->runRegisters->runStatus1;
    RUN_UINT_32 runStatus2 = runI2c->runRegisters->runStatus2;

    if ((runStatus & RUN_STM32F1_I2C_SR1_SB) != 0U)
    {
        RUN_UINT_32 runAddress = ((RUN_UINT_32)runI2c->runAddress << 1U);
        if (runI2c->runPhase == RUN_STM32F1_I2C_PHASE_READ)
        {
            runAddress |= 1U;
            runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_ACK;
            runI2c->runRegisters->runControl1 &= ~RUN_STM32F1_I2C_CR1_STOP;
        }
        /* Reading SR1 and writing DR clears SB and starts address transfer. */
        (RUN_VOID)runStatus2;
        runI2c->runRegisters->runData = runAddress;
        return;
    }

    if ((runStatus & RUN_STM32F1_I2C_SR1_ADDR) != 0U)
    {
        /* SR1 followed by SR2 clears ADDR and releases the clock stretch. */
        (RUN_VOID)runStatus;
        (RUN_VOID)runStatus2;
        if (runI2c->runPhase == RUN_STM32F1_I2C_PHASE_READ)
        {
            if (runI2c->runReadLength == 0U)
            {
                runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_STOP;
                run_stm32f1_i2c_complete(runI2c, RUN_STATUS_OK);
            }
            else if (runI2c->runReadLength == 1U)
            {
                /* NACK and STOP must be programmed before reading the only byte. */
                runI2c->runRegisters->runControl1 &= ~RUN_STM32F1_I2C_CR1_ACK;
                runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_STOP;
            }
        }
        return;
    }

    if ((runStatus & RUN_STM32F1_I2C_SR1_TXE) != 0U)
    {
        if ((runI2c->runPhase == RUN_STM32F1_I2C_PHASE_WRITE) &&
            (runI2c->runWriteIndex < runI2c->runWriteLength))
        {
            runI2c->runRegisters->runData =
                runI2c->runWriteData[runI2c->runWriteIndex++];
            return;
        }
    }

    if ((runStatus & RUN_STM32F1_I2C_SR1_BTF) != 0U)
    {
        if ((runI2c->runPhase == RUN_STM32F1_I2C_PHASE_WRITE) &&
            (runI2c->runWriteIndex == runI2c->runWriteLength))
        {
            if (runI2c->runReadLength != 0U)
            {
                /* BTF keeps the bus stretched while repeated START is issued. */
                runI2c->runPhase = RUN_STM32F1_I2C_PHASE_READ;
                runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_ACK;
                runI2c->runRegisters->runControl1 &= ~RUN_STM32F1_I2C_CR1_STOP;
                runI2c->runRegisters->runControl1 |=
                    RUN_STM32F1_I2C_CR1_START;
            }
            else
            {
                runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_STOP;
                run_stm32f1_i2c_complete(runI2c, RUN_STATUS_OK);
            }
            return;
        }
    }

    if ((runStatus & RUN_STM32F1_I2C_SR1_RXNE) != 0U)
    {
        if ((runI2c->runPhase == RUN_STM32F1_I2C_PHASE_READ) &&
            (runI2c->runReadIndex < runI2c->runReadLength))
        {
            if ((runI2c->runReadIndex + 1U) == runI2c->runReadLength)
            {
                /* Final byte: NACK and begin STOP before consuming DR. */
                runI2c->runRegisters->runControl1 &=
                    ~RUN_STM32F1_I2C_CR1_ACK;
                runI2c->runRegisters->runControl1 |=
                    RUN_STM32F1_I2C_CR1_STOP;
            }
            runI2c->runReadData[runI2c->runReadIndex++] =
                (RUN_UINT_8)runI2c->runRegisters->runData;
            if (runI2c->runReadIndex == runI2c->runReadLength)
            {
                run_stm32f1_i2c_complete(runI2c, RUN_STATUS_OK);
            }
        }
    }
}

/* Services I2C1 event interrupts for the active transaction. */
RUN_VOID I2C1_EV_IRQHandler(RUN_VOID)
{
    RunStm32f1I2c *runI2c = RunStm32f1I2cInstance;
    RUN_UINT_32 runErrors;
    if ((runI2c == RUN_NULL) || !runI2c->runBusy)
    {
        return;
    }
    runErrors = runI2c->runRegisters->runStatus1 & RUN_STM32F1_I2C_ERRORS;
    if (runErrors != 0U)
    {
        run_stm32f1_i2c_fail(runI2c, runErrors);
        return;
    }
    run_stm32f1_i2c_event(runI2c);
}

/* Services I2C1 error interrupts and terminates a failed transaction. */
RUN_VOID I2C1_ER_IRQHandler(RUN_VOID)
{
    RunStm32f1I2c *runI2c = RunStm32f1I2cInstance;
    RUN_UINT_32 runErrors;
    if ((runI2c == RUN_NULL) || !runI2c->runBusy)
    {
        return;
    }
    runErrors = runI2c->runRegisters->runStatus1 & RUN_STM32F1_I2C_ERRORS;
    if (runErrors != 0U)
    {
        run_stm32f1_i2c_fail(runI2c, runErrors);
    }
    else
    {
        (RUN_VOID)runI2c->runRegisters->runStatus2;
    }
}

/* Claims I2C1 and programs the first address phase of a transaction. */
RUN_STATIC RunStatus run_stm32f1_i2c_start(
    RunDevice *runDevice,
    RUN_UINT_16 runAddress,
    RUN_CONST RUN_UINT_8 *runWriteData,
    RUN_SIZE runWriteLength,
    RUN_UINT_8 *runReadData,
    RUN_SIZE runReadLength,
    RunI2cCompletion runCompletion,
    RUN_VOID *runArgument)
{
    RunStm32f1I2c *runI2c = (RunStm32f1I2c *)run_device_context(runDevice);
    RunCriticalState runState;
    if ((runI2c == RUN_NULL) || (runAddress > 0x7FU) ||
        ((runWriteLength == 0U) && (runReadLength == 0U)) ||
        ((runWriteData == RUN_NULL) && (runWriteLength != 0U)) ||
        ((runReadData == RUN_NULL) && (runReadLength != 0U)))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    if (run_kernel_is_in_isr())
    {
        return RUN_STATUS_CONTEXT;
    }
    run_stm32f1_i2c_initialize(runI2c);
    runState = run_arch_enter_critical();
    if (runI2c->runBusy ||
        ((runI2c->runRegisters->runStatus2 & RUN_STM32F1_I2C_SR2_BUSY) != 0U))
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_BUSY;
    }
    runI2c->runAddress = runAddress;
    runI2c->runWriteData = runWriteData;
    runI2c->runWriteLength = runWriteLength;
    runI2c->runWriteIndex = 0U;
    runI2c->runReadData = runReadData;
    runI2c->runReadLength = runReadLength;
    runI2c->runReadIndex = 0U;
    runI2c->runPhase = (runWriteLength != 0U) ?
        RUN_STM32F1_I2C_PHASE_WRITE : RUN_STM32F1_I2C_PHASE_READ;
    runI2c->runCompletion = runCompletion;
    runI2c->runCompletionArgument = runArgument;
    runI2c->runCompletionStatus = RUN_STATUS_OK;
    runI2c->runBusy = RUN_TRUE;
    runI2c->runRegisters->runControl1 |=
        RUN_STM32F1_I2C_CR1_PE | RUN_STM32F1_I2C_CR1_ACK;
    runI2c->runRegisters->runControl2 =
        RUN_STM32F1_I2C_CR2_FREQ_36MHZ | RUN_STM32F1_I2C_CR2_ITERREN |
        RUN_STM32F1_I2C_CR2_ITEVTEN | RUN_STM32F1_I2C_CR2_ITBUFEN;
    runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_START;
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

/* Aborts a timed-out transaction and discards any late completion token. */
RUN_STATIC RUN_VOID run_stm32f1_i2c_abort(RunStm32f1I2c *runI2c)
{
    RunCriticalState runState = run_arch_enter_critical();
    runI2c->runRegisters->runControl1 |= RUN_STM32F1_I2C_CR1_STOP;
    runI2c->runRegisters->runControl2 = RUN_STM32F1_I2C_CR2_FREQ_36MHZ;
    runI2c->runBusy = RUN_FALSE;
    runI2c->runWriteData = RUN_NULL;
    runI2c->runWriteLength = 0U;
    runI2c->runReadData = RUN_NULL;
    runI2c->runReadLength = 0U;
    runI2c->runCompletion = RUN_NULL;
    runI2c->runCompletionArgument = RUN_NULL;
    /* Drop a completion token delivered after the blocking take timed out. */
    (RUN_VOID)run_semaphore_take_from_isr(runI2c->runTransactionDone);
    run_arch_exit_critical(runState);
}

/* Runs a blocking I2C transaction through its completion semaphore. */
RUN_STATIC RunStatus run_stm32f1_i2c_transaction(
    RunDevice *runDevice,
    RUN_UINT_16 runAddress,
    RUN_CONST RUN_UINT_8 *runWriteData,
    RUN_SIZE runWriteLength,
    RUN_UINT_8 *runReadData,
    RUN_SIZE runReadLength,
    RUN_UINT_32 runTimeoutMs)
{
    RunStm32f1I2c *runI2c = (RunStm32f1I2c *)run_device_context(runDevice);
    RunStatus runStatus;
    RunTimeMs runWaitMs;
    if (runI2c == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runStatus = run_stm32f1_i2c_start(runDevice, runAddress, runWriteData,
                                      runWriteLength, runReadData, runReadLength,
                                      RUN_NULL, RUN_NULL);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runWaitMs = (runTimeoutMs == 0U) ? (RunTimeMs)1U : (RunTimeMs)runTimeoutMs;
    runStatus = run_semaphore_take(runI2c->runTransactionDone, runWaitMs);
    if (runStatus != RUN_STATUS_OK)
    {
        run_stm32f1_i2c_abort(runI2c);
    }
    else
    {
        runStatus = runI2c->runCompletionStatus;
    }
    return runStatus;
}

/* Starts a callback-completed I2C transaction in thread context. */
RUN_STATIC RunStatus run_stm32f1_i2c_transaction_async(
    RunDevice *runDevice,
    RUN_UINT_16 runAddress,
    RUN_CONST RUN_UINT_8 *runWriteData,
    RUN_SIZE runWriteLength,
    RUN_UINT_8 *runReadData,
    RUN_SIZE runReadLength,
    RunI2cCompletion runCompletion,
    RUN_VOID *runArgument)
{
    if (runCompletion == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    return run_stm32f1_i2c_start(runDevice, runAddress, runWriteData,
                                 runWriteLength, runReadData, runReadLength,
                                 runCompletion, runArgument);
}

RUN_STATIC RUN_CONST RunI2cOperations RunStm32f1I2cOperations = {
    run_stm32f1_i2c_transaction,
    run_stm32f1_i2c_transaction_async
};

/* Creates the singleton I2C1 device and enables its event and error IRQs. */
RunStatus run_i2c_create_f103(RUN_UINT_32 runIndex,
                              RUN_CONST RUN_CHAR *runName,
                              RunDevice **runDeviceOut)
{
    RunDeviceConfig runConfig;
    RunStatus runStatus;
    RunStm32f1I2c *runI2c;
    if ((runDeviceOut == RUN_NULL) || (runIndex != 1U) ||
        (RunStm32f1I2cInstance != RUN_NULL))
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
    runStatus = run_semaphore_create(0U, 1U, &runI2c->runTransactionDone);
    if (runStatus != RUN_STATUS_OK)
    {
        (RUN_VOID)run_device_delete(*runDeviceOut);
        *runDeviceOut = RUN_NULL;
        return runStatus;
    }
    RunStm32f1I2cInstance = runI2c;
    RunStm32f1I2cDevice = *runDeviceOut;
    run_arch_set_irq_priority(RUN_STM32F1_I2C_EVENT_IRQ,
                              RUN_STM32F1_I2C_IRQ_PRIORITY);
    run_arch_set_irq_priority(RUN_STM32F1_I2C_ERROR_IRQ,
                              RUN_STM32F1_I2C_IRQ_PRIORITY);
    run_arch_enable_irq(RUN_STM32F1_I2C_EVENT_IRQ);
    run_arch_enable_irq(RUN_STM32F1_I2C_ERROR_IRQ);
    return RUN_STATUS_OK;
}
