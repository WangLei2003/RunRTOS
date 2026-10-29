/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - STM32F1 polling SPI2 backend
 *
 * Layer: drivers/soc/stm32f1
 * Purpose: Perform master-mode full-duplex transfers with bounded polling.
 * Dependencies: public SPI interface and Cortex-M DWT cycle counter.
 * Constraint: Board code owns pin and chip-select setup; asynchronous completion
 *             currently returns UNSUPPORTED.
 */
#include "runrtos/arch/run_arch.h"
#include "runrtos/drivers/spi/run_spi.h"

#define RUN_STM32F1_SPI2_BASE 0x40003800U
#define RUN_STM32F1_SPI_CR1_CPHA (1UL << 0U)
#define RUN_STM32F1_SPI_CR1_CPOL (1UL << 1U)
#define RUN_STM32F1_SPI_CR1_MSTR (1UL << 2U)
#define RUN_STM32F1_SPI_CR1_SPE (1UL << 6U)
#define RUN_STM32F1_SPI_CR1_SSI (1UL << 8U)
#define RUN_STM32F1_SPI_CR1_SSM (1UL << 9U)
#define RUN_STM32F1_SPI_SR_RXNE (1UL << 0U)
#define RUN_STM32F1_SPI_SR_TXE (1UL << 1U)
#define RUN_STM32F1_SPI_SR_BSY (1UL << 7U)
#define RUN_STM32F1_SPI_BAUD_DIV_256 (3UL << 3U)
#define RUN_STM32F1_SPI_TIMEOUT_CYCLES 720000U

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
    RUN_BOOL runInitialized;
} RunStm32f1Spi;

RUN_STATIC RUN_BOOL run_stm32f1_spi_timed_out(RUN_UINT_32 runStart,
                                              RUN_UINT_32 runTimeoutMs)
{
    /* Zero selects a one-ms fallback rather than an instantaneous timeout. */
    RUN_UINT_32 runLimit = (runTimeoutMs == 0U) ?
        RUN_STM32F1_SPI_TIMEOUT_CYCLES :
        runTimeoutMs * (72000000U / 1000U);
    return ((run_arch_get_cycle_count() - runStart) > runLimit) ?
        RUN_TRUE : RUN_FALSE;
}

RUN_STATIC RunStatus run_stm32f1_spi_transfer(RunDevice *runDevice,
                                              RUN_CONST RUN_UINT_8 *runTransmit,
                                              RUN_UINT_8 *runReceive,
                                              RUN_SIZE runLength,
                                              RUN_UINT_32 runTimeoutMs)
{
    RunStm32f1Spi *runSpi = (RunStm32f1Spi *)run_device_context(runDevice);
    RUN_UINT_32 runStart;
    if (runSpi == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    if (!runSpi->runInitialized)
    {
        /* SSM/SSI replaces the unused NSS pin; DIV_256 gives a conservative clock. */
        runSpi->runRegisters->runControl1 =
            RUN_STM32F1_SPI_CR1_MSTR | RUN_STM32F1_SPI_CR1_SSM |
            RUN_STM32F1_SPI_CR1_SSI | RUN_STM32F1_SPI_BAUD_DIV_256;
        runSpi->runRegisters->runControl1 |= RUN_STM32F1_SPI_CR1_SPE;
        runSpi->runInitialized = RUN_TRUE;
    }
    runStart = run_arch_get_cycle_count();
    for (RUN_SIZE runIndex = 0U; runIndex < runLength; runIndex++)
    {
        /* 0xFF is the conventional idle byte when only receive is requested. */
        RUN_UINT_8 runByte = (runTransmit != RUN_NULL) ? runTransmit[runIndex] : 0xFFU;
        while ((runSpi->runRegisters->runStatus & RUN_STM32F1_SPI_SR_TXE) == 0U)
        {
            if (run_stm32f1_spi_timed_out(runStart, runTimeoutMs))
            {
                return RUN_STATUS_TIMEOUT;
            }
        }
        runSpi->runRegisters->runData = runByte;
        while ((runSpi->runRegisters->runStatus & RUN_STM32F1_SPI_SR_RXNE) == 0U)
        {
            if (run_stm32f1_spi_timed_out(runStart, runTimeoutMs))
            {
                return RUN_STATUS_TIMEOUT;
            }
        }
        runByte = (RUN_UINT_8)runSpi->runRegisters->runData;
        if (runReceive != RUN_NULL)
        {
            runReceive[runIndex] = runByte;
        }
    }
    /* TXE/RXNE can both be idle while the final shifter byte is still active. */
    while ((runSpi->runRegisters->runStatus & RUN_STM32F1_SPI_SR_BSY) != 0U)
    {
        if (run_stm32f1_spi_timed_out(runStart, runTimeoutMs))
        {
            return RUN_STATUS_TIMEOUT;
        }
    }
    return RUN_STATUS_OK;
}

RUN_STATIC RunStatus run_stm32f1_spi_transfer_async(RunDevice *runDevice,
                                                    RUN_CONST RUN_UINT_8 *runTransmit,
                                                    RUN_UINT_8 *runReceive,
                                                    RUN_SIZE runLength,
                                                    RunSpiCompletion runCompletion,
                                                    RUN_VOID *runArgument)
{
    (RUN_VOID)runDevice;
    (RUN_VOID)runTransmit;
    (RUN_VOID)runReceive;
    (RUN_VOID)runLength;
    (RUN_VOID)runCompletion;
    (RUN_VOID)runArgument;
    return RUN_STATUS_UNSUPPORTED;
}

RUN_STATIC RUN_CONST RunSpiOperations RunStm32f1SpiOperations = {
    run_stm32f1_spi_transfer,
    run_stm32f1_spi_transfer_async
};

RunStatus run_spi_create_f103(RUN_UINT_32 runIndex,
                              RUN_CONST RUN_CHAR *runName,
                              RunDevice **runDeviceOut)
{
    RunDeviceConfig runConfig;
    RunStatus runStatus;
    RunStm32f1Spi *runSpi;
    if ((runDeviceOut == RUN_NULL) || (runIndex != 2U))
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
    return RUN_STATUS_OK;
}

