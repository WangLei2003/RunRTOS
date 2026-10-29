/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - interrupt-driven STM32F1 USART1 backend
 *
 * Layer: drivers/soc/stm32f1
 * Purpose: Provide buffered RX, interrupt TXE/TC transmission, and async completion.
 * Dependencies: public UART contract, architecture IRQ controls, kernel semaphores.
 * Constraint: Only one transmitted buffer may be active; callbacks execute in the
 *             USART1 ISR, never block, and callers retain buffers until completion.
 */
#include "runrtos/arch/run_arch.h"
#include "runrtos/drivers/uart/run_uart.h"
#include "runrtos/kernel/run_sync.h"

#define RUN_STM32F1_USART1_BASE 0x40013800U
#define RUN_STM32F1_PCLK2_HZ 72000000U
#define RUN_STM32F1_USART_IRQ 37U
#define RUN_STM32F1_USART_IRQ_PRIORITY 0x80U
#define RUN_STM32F1_UART_RX_CAPACITY 256U
#define RUN_STM32F1_UART_RX_MASK (RUN_STM32F1_UART_RX_CAPACITY - 1U)
#define RUN_STM32F1_USART_SR_PE (1UL << 0U)
#define RUN_STM32F1_USART_SR_FE (1UL << 1U)
#define RUN_STM32F1_USART_SR_NE (1UL << 2U)
#define RUN_STM32F1_USART_SR_ORE (1UL << 3U)
#define RUN_STM32F1_USART_SR_RXNE (1UL << 5U)
#define RUN_STM32F1_USART_SR_TC (1UL << 6U)
#define RUN_STM32F1_USART_SR_TXE (1UL << 7U)
#define RUN_STM32F1_USART_CR1_RE (1UL << 2U)
#define RUN_STM32F1_USART_CR1_TE (1UL << 3U)
#define RUN_STM32F1_USART_CR1_RXNEIE (1UL << 5U)
#define RUN_STM32F1_USART_CR1_TCIE (1UL << 6U)
#define RUN_STM32F1_USART_CR1_TXEIE (1UL << 7U)
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
    RunSemaphore *runTransmitDone;
    RunSemaphore *runReceiveReady;
    RUN_CONST RUN_UINT_8 *runTransmitData;
    RUN_SIZE runTransmitLength;
    RUN_SIZE runTransmitIndex;
    RunUartCompletion runCompletion;
    RUN_VOID *runCompletionArgument;
    RUN_VOLATILE RUN_UINT_32 runReceiveHead;
    RUN_VOLATILE RUN_UINT_32 runReceiveTail;
    RUN_VOLATILE RUN_UINT_32 runReceiveOverflows;
    RUN_VOLATILE RUN_UINT_32 runLastError;
    RUN_VOLATILE RUN_BOOL runTransmitBusy;
    RUN_UINT_8 runReceiveBuffer[RUN_STM32F1_UART_RX_CAPACITY];
} RunStm32f1Uart;

RUN_STATIC RunStm32f1Uart *RunStm32f1UartInstance;
RUN_STATIC RunDevice *RunStm32f1UartDevice;

/* Services USART1 receive, transmit-empty, and transmit-complete events. */
RUN_VOID USART1_IRQHandler(RUN_VOID)
{
    RunStm32f1Uart *runUart = RunStm32f1UartInstance;
    RUN_UINT_32 runStatus;
    RUN_UINT_32 runControl;
    RUN_UINT_32 runNextHead;
    if (runUart == RUN_NULL)
    {
        return;
    }

    runStatus = runUart->runRegisters->runStatus;
    if ((runStatus & RUN_STM32F1_USART_SR_RXNE) != 0U)
    {
        RUN_UINT_8 runByte = (RUN_UINT_8)runUart->runRegisters->runData;
        runNextHead = (runUart->runReceiveHead + 1U) & RUN_STM32F1_UART_RX_MASK;
        if (runNextHead == runUart->runReceiveTail)
        {
            runUart->runReceiveOverflows++;
        }
        else
        {
            runUart->runReceiveBuffer[runUart->runReceiveHead] = runByte;
            runUart->runReceiveHead = runNextHead;
            (RUN_VOID)run_semaphore_give_from_isr(runUart->runReceiveReady);
        }
    }
    if ((runStatus & (RUN_STM32F1_USART_SR_PE |
                      RUN_STM32F1_USART_SR_FE |
                      RUN_STM32F1_USART_SR_NE |
                      RUN_STM32F1_USART_SR_ORE)) != 0U)
    {
        /* Reading SR then DR in the RX branch also clears overrun/error flags. */
        runUart->runLastError = runStatus;
    }
    /*
     * TXE and TC are handled as mutually exclusive phases. In particular, do
     * not consume an old TC value from before the final TDR write: that value
     * describes the previous transfer and could complete this one early.
     */
    runControl = runUart->runRegisters->runControl1;
    if (runUart->runTransmitBusy &&
        ((runControl & RUN_STM32F1_USART_CR1_TXEIE) != 0U) &&
        ((runStatus & RUN_STM32F1_USART_SR_TXE) != 0U))
    {
        if (runUart->runTransmitIndex < runUart->runTransmitLength)
        {
            runUart->runRegisters->runData =
                runUart->runTransmitData[runUart->runTransmitIndex++];
        }
        else
        {
            /* TDR acceptance is complete; the next boundary is TC. */
            runUart->runRegisters->runControl1 &=
                ~RUN_STM32F1_USART_CR1_TXEIE;
            runUart->runRegisters->runControl1 |=
                RUN_STM32F1_USART_CR1_TCIE;
        }
    }
    else if (runUart->runTransmitBusy &&
             ((runControl & RUN_STM32F1_USART_CR1_TCIE) != 0U) &&
             ((runStatus & RUN_STM32F1_USART_SR_TC) != 0U))
    {
        RunUartCompletion runCompletion = runUart->runCompletion;
        RUN_VOID *runCompletionArgument = runUart->runCompletionArgument;
        RUN_SIZE runTransferred = runUart->runTransmitLength;
        runUart->runRegisters->runControl1 &= ~RUN_STM32F1_USART_CR1_TCIE;
        /* Clear ownership before the callback so it may start the next transfer. */
        runUart->runTransmitBusy = RUN_FALSE;
        runUart->runTransmitData = RUN_NULL;
        runUart->runTransmitLength = 0U;
        runUart->runTransmitIndex = 0U;
        runUart->runCompletion = RUN_NULL;
        runUart->runCompletionArgument = RUN_NULL;
        if (runCompletion != RUN_NULL)
        {
            runCompletion(RunStm32f1UartDevice, RUN_STATUS_OK,
                          runTransferred, runCompletionArgument);
        }
        else
        {
            /* Blocking callers consume this token; async callers use the callback. */
            (RUN_VOID)run_semaphore_give_from_isr(runUart->runTransmitDone);
        }
    }
}

/* Programs USART1 frame format and baud divisor for the selected mode. */
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
    if (runUart->runTransmitBusy)
    {
        return RUN_STATUS_BUSY;
    }
    runDivider = (RUN_STM32F1_PCLK2_HZ + (runBaudRate / 2U)) / runBaudRate;
    runUart->runRegisters->runControl1 = 0U;
    runUart->runRegisters->runBaud = runDivider;
    runUart->runRegisters->runControl1 =
        RUN_STM32F1_USART_CR1_RE | RUN_STM32F1_USART_CR1_TE |
        RUN_STM32F1_USART_CR1_RXNEIE | RUN_STM32F1_USART_CR1_UE;
    run_arch_set_irq_priority(RUN_STM32F1_USART_IRQ,
                              RUN_STM32F1_USART_IRQ_PRIORITY);
    run_arch_enable_irq(RUN_STM32F1_USART_IRQ);
    return RUN_STATUS_OK;
}

/* Claims the transmitter and enables interrupts for an asynchronous write. */
RUN_STATIC RunStatus run_stm32f1_uart_start_async(
    RunDevice *runDevice,
    RUN_CONST RUN_UINT_8 *runData,
    RUN_SIZE runLength,
    RunUartCompletion runCompletion,
    RUN_VOID *runArgument)
{
    RunStm32f1Uart *runUart = (RunStm32f1Uart *)run_device_context(runDevice);
    RunCriticalState runState;
    if ((runUart == RUN_NULL) || (runData == RUN_NULL) || (runLength == 0U) ||
        run_kernel_is_in_isr())
    {
        return (runUart == RUN_NULL) || (runData == RUN_NULL) ||
               (runLength == 0U) ?
            RUN_STATUS_INVALID_ARGUMENT : RUN_STATUS_CONTEXT;
    }
    runState = run_arch_enter_critical();
    if (runUart->runTransmitBusy)
    {
        run_arch_exit_critical(runState);
        return RUN_STATUS_BUSY;
    }
    runUart->runTransmitBusy = RUN_TRUE;
    runUart->runTransmitData = runData;
    runUart->runTransmitLength = runLength;
    runUart->runTransmitIndex = 0U;
    runUart->runCompletion = runCompletion;
    runUart->runCompletionArgument = runArgument;
    runUart->runRegisters->runControl1 |= RUN_STM32F1_USART_CR1_TXEIE;
    run_arch_exit_critical(runState);
    return RUN_STATUS_OK;
}

/* Validates a callback and starts an asynchronous UART write. */
RUN_STATIC RunStatus run_stm32f1_uart_write_async(RunDevice *runDevice,
                                                  RUN_CONST RUN_UINT_8 *runData,
                                                  RUN_SIZE runLength,
                                                  RunUartCompletion runCompletion,
                                                  RUN_VOID *runArgument)
{
    if (runCompletion == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    return run_stm32f1_uart_start_async(runDevice, runData, runLength,
                                        runCompletion, runArgument);
}

/* Writes a buffer synchronously with timeout-safe transmit cancellation. */
RUN_STATIC RunStatus run_stm32f1_uart_write(RunDevice *runDevice,
                                            RUN_CONST RUN_UINT_8 *runData,
                                            RUN_SIZE runLength,
                                            RunTimeMs runTimeout)
{
    RunStm32f1Uart *runUart = (RunStm32f1Uart *)run_device_context(runDevice);
    RunStatus runStatus;
    if (runUart == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runStatus = run_stm32f1_uart_start_async(runDevice, runData, runLength,
                                             RUN_NULL, RUN_NULL);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    runStatus = run_semaphore_take(runUart->runTransmitDone, runTimeout);
    if (runStatus == RUN_STATUS_TIMEOUT)
    {
        RunCriticalState runState = run_arch_enter_critical();
        /* The caller may release the buffer after timeout, so detach it now. */
        if (runUart->runTransmitBusy)
        {
            runUart->runRegisters->runControl1 &=
                ~(RUN_STM32F1_USART_CR1_TXEIE | RUN_STM32F1_USART_CR1_TCIE);
            runUart->runTransmitBusy = RUN_FALSE;
            runUart->runTransmitData = RUN_NULL;
            runUart->runTransmitLength = 0U;
            runUart->runTransmitIndex = 0U;
            runUart->runCompletion = RUN_NULL;
        }
        /*
         * If the ISR completed between the timeout and this critical section,
         * consume its late token so it cannot satisfy the next transfer.
         */
        (RUN_VOID)run_semaphore_take_from_isr(runUart->runTransmitDone);
        run_arch_exit_critical(runState);
    }
    return runStatus;
}

/* Removes one byte from the receive ring while interrupts are masked. */
RUN_STATIC RUN_BOOL run_stm32f1_uart_pop(RunStm32f1Uart *runUart,
                                         RUN_UINT_8 *runByteOut)
{
    RunCriticalState runState = run_arch_enter_critical();
    RUN_BOOL runAvailable = RUN_FALSE;
    if (runUart->runReceiveTail != runUart->runReceiveHead)
    {
        *runByteOut = runUart->runReceiveBuffer[runUart->runReceiveTail];
        runUart->runReceiveTail =
            (runUart->runReceiveTail + 1U) & RUN_STM32F1_UART_RX_MASK;
        runAvailable = RUN_TRUE;
    }
    run_arch_exit_critical(runState);
    return runAvailable;
}

/* Reads available UART bytes with timeout and partial-read semantics. */
RUN_STATIC RunStatus run_stm32f1_uart_read(RunDevice *runDevice,
                                           RUN_UINT_8 *runData,
                                           RUN_SIZE runLength,
                                           RUN_SIZE *runReadOut,
                                           RunTimeMs runTimeout)
{
    RunStm32f1Uart *runUart = (RunStm32f1Uart *)run_device_context(runDevice);
    RUN_SIZE runRead = 0U;
    if ((runUart == RUN_NULL) || (runReadOut == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    while (runRead < runLength)
    {
        RunStatus runStatus =
            run_semaphore_take(runUart->runReceiveReady, runTimeout);
        if (runStatus != RUN_STATUS_OK)
        {
            *runReadOut = runRead;
            return (runRead == 0U) ? runStatus : RUN_STATUS_OK;
        }
        /*
         * Each posted token corresponds to a ring-buffer byte. Popping after
         * the take prevents a timeout/ISR race from leaving a stale token.
         */
        if (run_stm32f1_uart_pop(runUart, &runData[runRead]))
        {
            runRead++;
        }
    }
    *runReadOut = runRead;
    return RUN_STATUS_OK;
}

/* Waits for both the transmit queue and USART shift register to drain. */
RUN_STATIC RunStatus run_stm32f1_uart_flush(RunDevice *runDevice,
                                            RunTimeMs runTimeout)
{
    RunStm32f1Uart *runUart = (RunStm32f1Uart *)run_device_context(runDevice);
    if (runUart == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    if (runUart->runTransmitBusy)
    {
        RUN_UINT_32 runStart;
        RUN_UINT_32 runLimit;
        if (runTimeout == RUN_TIME_NO_WAIT)
        {
            return RUN_STATUS_BUSY;
        }
        runStart = run_arch_get_cycle_count();
        runLimit = (runTimeout == RUN_TIME_FOREVER) ?
            0xFFFFFFFFU : (RUN_UINT_32)(runTimeout * 72000U);
        while (runUart->runTransmitBusy)
        {
            if ((runTimeout != RUN_TIME_FOREVER) &&
                ((run_arch_get_cycle_count() - runStart) > runLimit))
            {
                return RUN_STATUS_TIMEOUT;
            }
        }
    }
    return ((runUart->runRegisters->runStatus & RUN_STM32F1_USART_SR_TC) != 0U) ?
        RUN_STATUS_OK : RUN_STATUS_BUSY;
}

RUN_STATIC RUN_CONST RunUartOperations RunStm32f1UartOperations = {
    run_stm32f1_uart_configure,
    run_stm32f1_uart_write,
    run_stm32f1_uart_write_async,
    run_stm32f1_uart_read,
    run_stm32f1_uart_flush
};

/* Creates the singleton USART1 device and its transmit/receive semaphores. */
RunStatus run_uart_create_f103(RUN_UINT_32 runIndex,
                               RUN_CONST RUN_CHAR *runName,
                               RunDevice **runDeviceOut)
{
    RunDeviceConfig runConfig;
    RunStatus runStatus;
    RunStm32f1Uart *runUart;
    if ((runDeviceOut == RUN_NULL) || (runIndex != 1U) ||
        (RunStm32f1UartInstance != RUN_NULL))
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
    runStatus = run_semaphore_create(0U, 1U, &runUart->runTransmitDone);
    if (runStatus == RUN_STATUS_OK)
    {
        /*
         * One token corresponds to one ring-buffer byte. The full capacity
         * prevents stale tokens when a caller drains several bytes at once.
         */
        runStatus = run_semaphore_create(0U, RUN_STM32F1_UART_RX_CAPACITY,
                                         &runUart->runReceiveReady);
    }
    if (runStatus != RUN_STATUS_OK)
    {
        (RUN_VOID)run_device_delete(*runDeviceOut);
        *runDeviceOut = RUN_NULL;
        return runStatus;
    }
    RunStm32f1UartInstance = runUart;
    RunStm32f1UartDevice = *runDeviceOut;
    return RUN_STATUS_OK;
}
