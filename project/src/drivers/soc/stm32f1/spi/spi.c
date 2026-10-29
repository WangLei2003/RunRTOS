/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - interrupt-driven STM32F1 SPI2 backend
 *
 * Layer: drivers/soc/stm32f1
 * Purpose: Perform single-owner full-duplex transfers through TXE/RXNE interrupts.
 * Dependencies: public SPI contract, architecture IRQ controls, and semaphores.
 * Constraint: One transfer may be active per controller; the callback runs in the
 *             SPI2 ISR, never blocks, and buffers remain caller-owned until then.
 */
#include "runrtos/arch/run_arch.h"
#include "runrtos/drivers/spi/run_spi.h"
#include "runrtos/kernel/run_sync.h"

#define RUN_STM32F1_SPI2_BASE 0x40003800U
#define RUN_STM32F1_SPI_IRQ 36U
#define RUN_STM32F1_SPI_IRQ_PRIORITY 0x80U
#define RUN_STM32F1_SPI_CR1_MSTR (1UL << 2U)
#define RUN_STM32F1_SPI_CR1_SPE (1UL << 6U)
#define RUN_STM32F1_SPI_CR1_SSI (1UL << 8U)
#define RUN_STM32F1_SPI_CR1_SSM (1UL << 9U)
#define RUN_STM32F1_SPI_BAUD_DIV_256 (3UL << 3U)
#define RUN_STM32F1_SPI_CR2_ERRIE (1UL << 5U)
#define RUN_STM32F1_SPI_CR2_RXNEIE (1UL << 6U)
#define RUN_STM32F1_SPI_CR2_TXEIE (1UL << 7U)
#define RUN_STM32F1_SPI_SR_RXNE (1UL << 0U)
#define RUN_STM32F1_SPI_SR_TXE (1UL << 1U)
#define RUN_STM32F1_SPI_SR_CRCERR (1UL << 4U)
#define RUN_STM32F1_SPI_SR_MODF (1UL << 5U)
#define RUN_STM32F1_SPI_SR_OVR (1UL << 6U)
#define RUN_STM32F1_SPI_ERRORS \
    (RUN_STM32F1_SPI_SR_CRCERR | RUN_STM32F1_SPI_SR_MODF | RUN_STM32F1_SPI_SR_OVR)

typedef RUN_STRUCT RunStm32f1SpiRegisters
{
    RUN_VOLATILE RUN_UINT_32 runControl1;
    RUN_VOLATILE RUN_UINT_32 runControl2;
    RUN_VOLATILE RUN_UINT_32 runStatus;
    RUN_VOLATILE RUN_UINT_32 runData;
    RUN_VOLATILE RUN_UINT_32 runCrcPolynomial;
    RUN_VOLATILE RUN_UINT_32 runRxCrc;
    RUN_VOLATILE RUN_UINT_32 runTxCrc;
    RUN_VOLATILE RUN_UINT_32 runI2sConfig;
    RUN_VOLATILE RUN_UINT_32 runI2sPrescaler;
} RunStm32f1SpiRegisters;

typedef RUN_STRUCT RunStm32f1Spi
{
    RunStm32f1SpiRegisters *runRegisters;
    RunSemaphore *runTransferDone;
    RUN_CONST RUN_UINT_8 *runTransmit;
    RUN_UINT_8 *runReceive;
    RUN_SIZE runLength;
    RUN_SIZE runTransmitIndex;
    RUN_SIZE runReceiveIndex;
    RunSpiCompletion runCompletion;
    RUN_VOID *runCompletionArgument;
    RunStatus runCompletionStatus;
    RUN_VOLATILE RUN_BOOL runBusy;
} RunStm32f1Spi;

RUN_STATIC RunStm32f1Spi *RunStm32f1SpiInstance;
RUN_STATIC RunDevice *RunStm32f1SpiDevice;

/* Clears SPI overrun and stale receive state before a new transfer. */
RUN_STATIC RUN_VOID run_stm32f1_spi_clear_errors(RunStm32f1Spi *runSpi)
{
    /* OVR is cleared by reading DR followed by SR. */
    if ((runSpi->runRegisters->runStatus & RUN_STM32F1_SPI_SR_RXNE) != 0U)
    {
        (RUN_VOID)runSpi->runRegisters->runData;
    }
    (RUN_VOID)runSpi->runRegisters->runStatus;
    (RUN_VOID)runSpi->runRegisters->runData;
    (RUN_VOID)runSpi->runRegisters->runStatus;
}

/* Releases SPI ownership and signals the blocking or asynchronous waiter. */
RUN_STATIC RUN_VOID run_stm32f1_spi_complete(RunStm32f1Spi *runSpi,
                                             RunStatus runStatus)
{
    RunSpiCompletion runCompletion = runSpi->runCompletion;
    RUN_VOID *runArgument = runSpi->runCompletionArgument;

    runSpi->runRegisters->runControl2 = 0U;
    runSpi->runCompletionStatus = runStatus;
    runSpi->runBusy = RUN_FALSE;
    runSpi->runTransmit = RUN_NULL;
    runSpi->runReceive = RUN_NULL;
    runSpi->runLength = 0U;
    runSpi->runCompletion = RUN_NULL;
    runSpi->runCompletionArgument = RUN_NULL;
    if (runCompletion != RUN_NULL)
    {
        /* Release ownership first so the callback can submit a new transfer. */
        runCompletion(RunStm32f1SpiDevice, runStatus, runArgument);
    }
    else
    {
        (RUN_VOID)run_semaphore_give_from_isr(runSpi->runTransferDone);
    }
}

/* Moves bytes through TXE/RXNE and completes or fails the active transfer. */
RUN_VOID SPI2_IRQHandler(RUN_VOID)
{
    RunStm32f1Spi *runSpi = RunStm32f1SpiInstance;
    RUN_UINT_32 runStatus;

    if ((runSpi == RUN_NULL) || !runSpi->runBusy)
    {
        return;
    }
    runStatus = runSpi->runRegisters->runStatus;
    if ((runStatus & RUN_STM32F1_SPI_ERRORS) != 0U)
    {
        run_stm32f1_spi_clear_errors(runSpi);
        run_stm32f1_spi_complete(runSpi, RUN_STATUS_ERROR);
        return;
    }

    if (((runStatus & RUN_STM32F1_SPI_SR_RXNE) != 0U) &&
        (runSpi->runReceiveIndex < runSpi->runLength))
    {
        RUN_UINT_8 runByte = (RUN_UINT_8)runSpi->runRegisters->runData;
        if (runSpi->runReceive != RUN_NULL)
        {
            runSpi->runReceive[runSpi->runReceiveIndex] = runByte;
        }
        runSpi->runReceiveIndex++;
    }
    if (((runStatus & RUN_STM32F1_SPI_SR_TXE) != 0U) &&
        (runSpi->runTransmitIndex < runSpi->runLength))
    {
        RUN_UINT_8 runByte = (runSpi->runTransmit != RUN_NULL) ?
            runSpi->runTransmit[runSpi->runTransmitIndex] : 0xFFU;
        runSpi->runRegisters->runData = runByte;
        runSpi->runTransmitIndex++;
    }
    if ((runSpi->runTransmitIndex == runSpi->runLength) &&
        (runSpi->runReceiveIndex == runSpi->runLength))
    {
        run_stm32f1_spi_complete(runSpi, RUN_STATUS_OK);
    }
}

/* Enables SPI2 as a software-controlled master at the board's safe clock. */
RUN_STATIC RunStatus run_stm32f1_spi_initialize(RunStm32f1Spi *runSpi)
{
    RunCriticalState runState = run_arch_enter_critical();
    if ((runSpi->runRegisters->runControl1 & RUN_STM32F1_SPI_CR1_SPE) == 0U)
    {
        runSpi->runRegisters->runControl1 = 0U;
        runSpi->runRegisters->runControl2 = 0U;
        runSpi->runRegisters->runControl1 =
            RUN_STM32F1_SPI_CR1_MSTR | RUN_STM32F1_SPI_CR1_SSM |
            RUN_STM32F1_SPI_CR1_SSI | RUN_STM32F1_SPI_BAUD_DIV_256;
        runSpi->runRegisters->runControl1 |= RUN_STM32F1_SPI_CR1_SPE;
    }
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

/* Claims SPI2 and enables TXE, RXNE, and error interrupt sources. */
RUN_STATIC RunStatus run_stm32f1_spi_start(RunDevice *runDevice,
                                           RUN_CONST RUN_UINT_8 *runTransmit,
                                           RUN_UINT_8 *runReceive,
                                           RUN_SIZE runLength,
                                           RunSpiCompletion runCompletion,
                                           RUN_VOID *runArgument)
{
    RunStm32f1Spi *runSpi = (RunStm32f1Spi *)run_device_context(runDevice);
    RunCriticalState runState;
    RunStatus runStatus;

    if ((runSpi == RUN_NULL) || (runLength == 0U) ||
        ((runTransmit == RUN_NULL) && (runReceive == RUN_NULL)))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    if (run_kernel_is_in_isr())
    {
        return RUN_STATUS_CONTEXT;
    }
    runStatus = run_stm32f1_spi_initialize(runSpi);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runState = run_arch_enter_critical();
    if (runSpi->runBusy)
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_BUSY;
    }
    run_stm32f1_spi_clear_errors(runSpi);
    runSpi->runBusy = RUN_TRUE;
    runSpi->runTransmit = runTransmit;
    runSpi->runReceive = runReceive;
    runSpi->runLength = runLength;
    runSpi->runTransmitIndex = 0U;
    runSpi->runReceiveIndex = 0U;
    runSpi->runCompletion = runCompletion;
    runSpi->runCompletionArgument = runArgument;
    runSpi->runCompletionStatus = RUN_STATUS_OK;
    runSpi->runRegisters->runControl2 =
        RUN_STM32F1_SPI_CR2_ERRIE | RUN_STM32F1_SPI_CR2_RXNEIE |
        RUN_STM32F1_SPI_CR2_TXEIE;
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

/* Aborts a timed-out transfer and discards any late completion token. */
RUN_STATIC RUN_VOID run_stm32f1_spi_abort(RunStm32f1Spi *runSpi)
{
    RunCriticalState runState = run_arch_enter_critical();
    runSpi->runRegisters->runControl2 = 0U;
    runSpi->runBusy = RUN_FALSE;
    runSpi->runTransmit = RUN_NULL;
    runSpi->runReceive = RUN_NULL;
    runSpi->runLength = 0U;
    runSpi->runCompletion = RUN_NULL;
    runSpi->runCompletionArgument = RUN_NULL;
    run_stm32f1_spi_clear_errors(runSpi);
    /* Drop a completion token delivered after the blocking take timed out. */
    (RUN_VOID)run_semaphore_take_from_isr(runSpi->runTransferDone);
    run_arch_exit_critical(runState);
}

/* Runs a blocking full-duplex transfer through interrupt completion. */
RUN_STATIC RunStatus run_stm32f1_spi_transfer(RunDevice *runDevice,
                                              RUN_CONST RUN_UINT_8 *runTransmit,
                                              RUN_UINT_8 *runReceive,
                                              RUN_SIZE runLength,
                                              RUN_UINT_32 runTimeoutMs)
{
    RunStm32f1Spi *runSpi = (RunStm32f1Spi *)run_device_context(runDevice);
    RunStatus runStatus;
    RunTimeMs runWaitMs;
    if (runSpi == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runStatus = run_stm32f1_spi_start(runDevice, runTransmit, runReceive,
                                      runLength, RUN_NULL, RUN_NULL);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runWaitMs = (runTimeoutMs == 0U) ? (RunTimeMs)1U : (RunTimeMs)runTimeoutMs;
    runStatus = run_semaphore_take(runSpi->runTransferDone, runWaitMs);
    if (runStatus != RUN_STATUS_OK)
    {
        run_stm32f1_spi_abort(runSpi);
    }
    else
    {
        runStatus = runSpi->runCompletionStatus;
    }
    return runStatus;
}

/* Starts a callback-completed full-duplex SPI transfer. */
RUN_STATIC RunStatus run_stm32f1_spi_transfer_async(RunDevice *runDevice,
                                                    RUN_CONST RUN_UINT_8 *runTransmit,
                                                    RUN_UINT_8 *runReceive,
                                                    RUN_SIZE runLength,
                                                    RunSpiCompletion runCompletion,
                                                    RUN_VOID *runArgument)
{
    if (runCompletion == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    return run_stm32f1_spi_start(runDevice, runTransmit, runReceive, runLength,
                                 runCompletion, runArgument);
}

RUN_STATIC RUN_CONST RunSpiOperations RunStm32f1SpiOperations = {
    run_stm32f1_spi_transfer,
    run_stm32f1_spi_transfer_async
};

/* Creates the singleton SPI2 device and enables its NVIC interrupt. */
RunStatus run_spi_create_f103(RUN_UINT_32 runIndex,
                              RUN_CONST RUN_CHAR *runName,
                              RunDevice **runDeviceOut)
{
    RunDeviceConfig runConfig;
    RunStatus runStatus;
    RunStm32f1Spi *runSpi;
    if ((runDeviceOut == RUN_NULL) || (runIndex != 2U) ||
        (RunStm32f1SpiInstance != RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runConfig.runName = runName;
    runConfig.runClass = RUN_DEVICE_CLASS_SPI;
    runConfig.runOperations = &RunStm32f1SpiOperations;
    runConfig.runContextSize = sizeof(RunStm32f1Spi);
    runStatus = run_device_create(&runConfig, runDeviceOut);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runSpi = (RunStm32f1Spi *)run_device_context(*runDeviceOut);
    runSpi->runRegisters =
        (RunStm32f1SpiRegisters *)(RUN_VOID *)RUN_STM32F1_SPI2_BASE;
    runStatus = run_semaphore_create(0U, 1U, &runSpi->runTransferDone);
    if (runStatus != RUN_STATUS_OK)
    {
        (RUN_VOID)run_device_delete(*runDeviceOut);
        *runDeviceOut = RUN_NULL;
        return runStatus;
    }
    RunStm32f1SpiInstance = runSpi;
    RunStm32f1SpiDevice = *runDeviceOut;
    run_arch_set_irq_priority(RUN_STM32F1_SPI_IRQ, RUN_STM32F1_SPI_IRQ_PRIORITY);
    run_arch_enable_irq(RUN_STM32F1_SPI_IRQ);
    return RUN_STATUS_OK;
}
