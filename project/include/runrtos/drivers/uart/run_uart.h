/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - UART controller capability interface
 *
 * Layer: drivers
 * Purpose: Expose configuration, stream I/O, and asynchronous completion.
 * Dependencies: generic RunDevice framework.
 * Constraint: Caller-owned buffers must remain valid until a synchronous call
 *             returns or an asynchronous completion callback fires.
 */
#ifndef RUN_UART_H_
#define RUN_UART_H_

#include "runrtos/drivers/run_device.h"

typedef RUN_VOID (*RunUartCompletion)(RunDevice *runDevice,
                                      RunStatus runStatus,
                                      RUN_SIZE runTransferred,
                                      RUN_VOID *runArgument);

typedef RUN_STRUCT RunUartOperations
{
    RunStatus (*runConfigure)(RunDevice *runDevice,
                              RUN_UINT_32 runBaudRate,
                              RUN_UINT_8 runDataBits,
                              RUN_UINT_8 runParity,
                              RUN_UINT_8 runStopBits);
    RunStatus (*runWrite)(RunDevice *runDevice,
                          RUN_CONST RUN_UINT_8 *runData,
                          RUN_SIZE runLength,
                          RunTimeMs runTimeout);
    RunStatus (*runWriteAsync)(RunDevice *runDevice,
                               RUN_CONST RUN_UINT_8 *runData,
                               RUN_SIZE runLength,
                               RunUartCompletion runCompletion,
                               RUN_VOID *runArgument);
    RunStatus (*runRead)(RunDevice *runDevice,
                         RUN_UINT_8 *runData,
                         RUN_SIZE runLength,
                         RUN_SIZE *runReadOut,
                         RunTimeMs runTimeout);
    RunStatus (*runFlush)(RunDevice *runDevice, RunTimeMs runTimeout);
} RunUartOperations;

RUN_STATIC RUN_INLINE RUN_CONST RunUartOperations *run_uart_ops(RunDevice *runDevice)
{
    return (RUN_CONST RunUartOperations *)run_device_operations(runDevice);
}

/*
 * Synchronous calls wait in the backend until completion or timeout. Async calls
 * return after the transfer is accepted; the input buffer and callback argument
 * must stay valid until completion. Backends without async support return
 * RUN_STATUS_UNSUPPORTED synchronously and do not schedule a callback.
 */
RunStatus run_uart_configure(RunDevice *runDevice,
                             RUN_UINT_32 runBaudRate,
                             RUN_UINT_8 runDataBits,
                             RUN_UINT_8 runParity,
                             RUN_UINT_8 runStopBits);
RunStatus run_uart_write(RunDevice *runDevice,
                         RUN_CONST RUN_UINT_8 *runData,
                         RUN_SIZE runLength,
                         RunTimeMs runTimeout);
RunStatus run_uart_write_async(RunDevice *runDevice,
                               RUN_CONST RUN_UINT_8 *runData,
                               RUN_SIZE runLength,
                               RunUartCompletion runCompletion,
                               RUN_VOID *runArgument);
RunStatus run_uart_read(RunDevice *runDevice,
                        RUN_UINT_8 *runData,
                        RUN_SIZE runLength,
                        RUN_SIZE *runReadOut,
                        RunTimeMs runTimeout);

/* Waits until the transmitter shift register drains, not merely FIFO acceptance. */
RunStatus run_uart_flush(RunDevice *runDevice, RunTimeMs runTimeout);

RunStatus run_uart_create_f103(RUN_UINT_32 runIndex,
                               RUN_CONST RUN_CHAR *runName,
                               RunDevice **runDeviceOut);

#endif

