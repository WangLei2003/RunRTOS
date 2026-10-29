/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - STM32F1 polling USART1 backend
 *
 * Layer: drivers/soc/stm32f1
 * Purpose: Configure 8N1 and perform bounded polling I/O through USART1.
 * Dependencies: public UART interface and Cortex-M DWT cycle counter.
 * Constraint: Assumes board setup provides a 72 MHz PCLK2 and PA9/PA10 alternate
 *             function pins; asynchronous transfers currently return UNSUPPORTED.
 */
#include "runrtos/arch/run_arch.h"
#include "runrtos/drivers/uart/run_uart.h"

#define RUN_STM32F1_USART1_BASE 0x40013800U
#define RUN_STM32F1_PCLK2_HZ 72000000U
#define RUN_STM32F1_USART_SR_TXE (1UL << 7U)
#define RUN_STM32F1_USART_SR_RXNE (1UL << 5U)
#define RUN_STM32F1_USART_SR_TC (1UL << 6U)
#define RUN_STM32F1_USART_CR1_RE (1UL << 2U)
#define RUN_STM32F1_USART_CR1_TE (1UL << 3U)
#define RUN_STM32F1_USART_CR1_UE (1UL << 13U)

typedef RUN_STRUCT RunStm32f1UartRegisters
{
    RUN_VOLATILE RUN_UINT_32 runStatus;
    RUN_VOLATILE RUN_UINT_32 runData;
    RUN_VOLATILE RUN_UINT_32 runBaud;
    RUN_VOLATILE RUN_UINT_32 runControl1;
    RUN_VOLATILE RUN_UINT_32 runControl2;
    RUN_VOLATILE RUN_UINT_32 runControl3;
    RUN_VOLATILE RUN_UINT_32 runGuardTime;
} RunStm32f1UartRegisters;

typedef RUN_STRUCT RunStm32f1Uart
{
    RunStm32f1UartRegisters *runRegisters;
} RunStm32f1Uart;

RUN_STATIC RUN_BOOL run_stm32f1_uart_timed_out(RUN_UINT_32 runStart,
                                               RUN_UINT_32 runTimeoutMs)
{
    RUN_UINT_32 runElapsed;
    RUN_UINT_32 runLimit;
    /* A saturated timeout means wait forever; DWT wraps via unsigned subtraction. */
    if (runTimeoutMs == 0xFFFFFFFFU)
    {
        return RUN_FALSE;
    }
    runElapsed = run_arch_get_cycle_count() - runStart;
    runLimit = runTimeoutMs * (RUN_STM32F1_PCLK2_HZ / 1000U);
    return (runElapsed > runLimit) ? RUN_TRUE : RUN_FALSE;
}

RUN_STATIC RunStatus run_stm32f1_uart_configure(RunDevice *runDevice,
                                                RUN_UINT_32 runBaudRate,
                                                RUN_UINT_8 runDataBits,
                                                RUN_UINT_8 runParity,
                                                RUN_UINT_8 runStopBits)
{
    RunStm32f1Uart *runUart = (RunStm32f1Uart *)run_device_context(runDevice);
    RUN_UINT_32 runDivider;
    if ((runUart == RUN_NULL) || (runBaudRate == 0U) ||
        (runDataBits != 8U) || (runParity != 0U) || (runStopBits != 1U))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    /* Round to the nearest integer divider to minimize baud-rate error. */
    runDivider = (RUN_STM32F1_PCLK2_HZ + (runBaudRate / 2U)) / runBaudRate;
    runUart->runRegisters->runControl1 = 0U;
    runUart->runRegisters->runBaud = runDivider;
    runUart->runRegisters->runControl1 =
        RUN_STM32F1_USART_CR1_RE | RUN_STM32F1_USART_CR1_TE |
        RUN_STM32F1_USART_CR1_UE;
    return RUN_STATUS_OK;
}

RUN_STATIC RunStatus run_stm32f1_uart_write(RunDevice *runDevice,
                                            RUN_CONST RUN_UINT_8 *runData,
                                            RUN_SIZE runLength,
                                            RunTimeMs runTimeout)
{
    RunStm32f1Uart *runUart = (RunStm32f1Uart *)run_device_context(runDevice);
    RUN_UINT_32 runStart = run_arch_get_cycle_count();
    if (runUart == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    /*
     * The API completes once all bytes are accepted into the transmitter.
     * run_uart_flush waits for the final byte to leave the shift register.
     */
    for (RUN_SIZE runIndex = 0U; runIndex < runLength; runIndex++)
    {
        while ((runUart->runRegisters->runStatus & RUN_STM32F1_USART_SR_TXE) == 0U)
        {
            if (run_stm32f1_uart_timed_out(runStart, (RUN_UINT_32)runTimeout))
            {
                return RUN_STATUS_TIMEOUT;
            }
        }
        runUart->runRegisters->runData = runData[runIndex];
    }
    return RUN_STATUS_OK;
}

RUN_STATIC RunStatus run_stm32f1_uart_write_async(RunDevice *runDevice,
                                                  RUN_CONST RUN_UINT_8 *runData,
                                                  RUN_SIZE runLength,
                                                  RunUartCompletion runCompletion,
                                                  RUN_VOID *runArgument)
{
    (RUN_VOID)runDevice;
    (RUN_VOID)runData;
    (RUN_VOID)runLength;
    (RUN_VOID)runCompletion;
    (RUN_VOID)runArgument;
    return RUN_STATUS_UNSUPPORTED;
}

RUN_STATIC RunStatus run_stm32f1_uart_read(RunDevice *runDevice,
                                           RUN_UINT_8 *runData,
                                           RUN_SIZE runLength,
                                           RUN_SIZE *runReadOut,
                                           RunTimeMs runTimeout)
{
    RunStm32f1Uart *runUart = (RunStm32f1Uart *)run_device_context(runDevice);
    RUN_UINT_32 runStart = run_arch_get_cycle_count();
    RUN_SIZE runRead = 0U;
    if ((runUart == RUN_NULL) || (runReadOut == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    while (runRead < runLength)
    {
        if ((runUart->runRegisters->runStatus & RUN_STM32F1_USART_SR_RXNE) != 0U)
        {
            runData[runRead++] = (RUN_UINT_8)runUart->runRegisters->runData;
        }
        else if (run_stm32f1_uart_timed_out(runStart, (RUN_UINT_32)runTimeout))
        {
            /* Preserve a useful partial result instead of discarding received bytes. */
            *runReadOut = runRead;
            return (runRead == 0U) ? RUN_STATUS_TIMEOUT : RUN_STATUS_OK;
        }
    }
    *runReadOut = runRead;
    return RUN_STATUS_OK;
}

RUN_STATIC RunStatus run_stm32f1_uart_flush(RunDevice *runDevice, RunTimeMs runTimeout)
{
    RunStm32f1Uart *runUart = (RunStm32f1Uart *)run_device_context(runDevice);
    RUN_UINT_32 runStart = run_arch_get_cycle_count();
    if (runUart == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    while ((runUart->runRegisters->runStatus & RUN_STM32F1_USART_SR_TC) == 0U)
    {
        if (run_stm32f1_uart_timed_out(runStart, (RUN_UINT_32)runTimeout))
        {
            return RUN_STATUS_TIMEOUT;
        }
    }
    return RUN_STATUS_OK;
}

RUN_STATIC RUN_CONST RunUartOperations RunStm32f1UartOperations = {
    run_stm32f1_uart_configure,
    run_stm32f1_uart_write,
    run_stm32f1_uart_write_async,
    run_stm32f1_uart_read,
    run_stm32f1_uart_flush
};

RunStatus run_uart_create_f103(RUN_UINT_32 runIndex,
                               RUN_CONST RUN_CHAR *runName,
                               RunDevice **runDeviceOut)
{
    RunDeviceConfig runConfig;
    RunStatus runStatus;
    RunStm32f1Uart *runUart;
    if ((runDeviceOut == RUN_NULL) || (runIndex != 1U))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runConfig.runName = runName;
    runConfig.runClass = RUN_DEVICE_CLASS_UART;
    runConfig.runOperations = &RunStm32f1UartOperations;
    runConfig.runContextSize = sizeof(RunStm32f1Uart);
    runStatus = run_device_create(&runConfig, runDeviceOut);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runUart = (RunStm32f1Uart *)run_device_context(*runDeviceOut);
    runUart->runRegisters =
        (RunStm32f1UartRegisters *)(RUN_VOID *)RUN_STM32F1_USART1_BASE;
    return RUN_STATUS_OK;
}

