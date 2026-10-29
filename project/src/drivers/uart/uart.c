/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - UART capability forwarding
 *
 * Layer: drivers
 * Purpose: Validate configuration and buffer arguments before backend dispatch.
 * Dependencies: public UART/device interfaces.
 * Constraint: Buffer ownership remains with the caller; async completion semantics
 *             are defined by the concrete UART operation table.
 */
#include "runrtos/drivers/uart/run_uart.h"

/* Configures UART framing and baud rate through the selected backend. */
RunStatus run_uart_configure(RunDevice *runDevice,
                             RUN_UINT_32 runBaudRate,
                             RUN_UINT_8 runDataBits,
                             RUN_UINT_8 runParity,
                             RUN_UINT_8 runStopBits)
{
    RUN_CONST RunUartOperations *runOperations = run_uart_ops(runDevice);
    if ((runOperations == RUN_NULL) || (runOperations->runConfigure == RUN_NULL))
    {
        return RUN_STATUS_UNSUPPORTED;
    }
    return runOperations->runConfigure(runDevice, runBaudRate, runDataBits,
                                       runParity, runStopBits);
}

/* Transmits a caller-owned buffer and waits for backend completion or timeout. */
RunStatus run_uart_write(RunDevice *runDevice,
                         RUN_CONST RUN_UINT_8 *runData,
                         RUN_SIZE runLength,
                         RunTimeMs runTimeout)
{
    RUN_CONST RunUartOperations *runOperations = run_uart_ops(runDevice);
    if ((runOperations == RUN_NULL) || (runOperations->runWrite == RUN_NULL) ||
        ((runData == RUN_NULL) && (runLength != 0U)))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    return runOperations->runWrite(runDevice, runData, runLength, runTimeout);
}

/* Starts nonblocking transmission while the caller retains buffer ownership. */
RunStatus run_uart_write_async(RunDevice *runDevice,
                               RUN_CONST RUN_UINT_8 *runData,
                               RUN_SIZE runLength,
                               RunUartCompletion runCompletion,
                               RUN_VOID *runArgument)
{
    RUN_CONST RunUartOperations *runOperations = run_uart_ops(runDevice);
    if ((runOperations == RUN_NULL) || (runOperations->runWriteAsync == RUN_NULL))
    {
        return RUN_STATUS_UNSUPPORTED;
    }
    return runOperations->runWriteAsync(runDevice, runData, runLength,
                                        runCompletion, runArgument);
}

/* Receives available bytes, preserving partial-read semantics on timeout. */
RunStatus run_uart_read(RunDevice *runDevice,
                        RUN_UINT_8 *runData,
                        RUN_SIZE runLength,
                        RUN_SIZE *runReadOut,
                        RunTimeMs runTimeout)
{
    RUN_CONST RunUartOperations *runOperations = run_uart_ops(runDevice);
    if ((runOperations == RUN_NULL) || (runOperations->runRead == RUN_NULL) ||
        ((runData == RUN_NULL) && (runLength != 0U)))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    return runOperations->runRead(runDevice, runData, runLength, runReadOut,
                                  runTimeout);
}

/* Waits until the UART transmitter has fully drained. */
RunStatus run_uart_flush(RunDevice *runDevice, RunTimeMs runTimeout)
{
    RUN_CONST RunUartOperations *runOperations = run_uart_ops(runDevice);
    if ((runOperations == RUN_NULL) || (runOperations->runFlush == RUN_NULL))
    {
        return RUN_STATUS_UNSUPPORTED;
    }
    return runOperations->runFlush(runDevice, runTimeout);
}

