/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - SPI controller capability interface
 *
 * Layer: drivers
 * Purpose: Expose full-duplex transfers with optional completion callbacks.
 * Dependencies: generic RunDevice framework.
 * Constraint: Transfer buffers remain caller-owned; chip-select policy belongs
 *             to the board or upper device driver.
 */
#ifndef RUN_SPI_H_
#define RUN_SPI_H_

#include "runrtos/drivers/run_device.h"

typedef RUN_VOID (*RunSpiCompletion)(RunDevice *runDevice,
                                     RunStatus runStatus,
                                     RUN_VOID *runArgument);

typedef RUN_STRUCT RunSpiOperations
{
    RunStatus (*runTransfer)(RunDevice *runDevice,
                             RUN_CONST RUN_UINT_8 *runTransmit,
                             RUN_UINT_8 *runReceive,
                             RUN_SIZE runLength,
                             RUN_UINT_32 runTimeoutMs);
    RunStatus (*runTransferAsync)(RunDevice *runDevice,
                                  RUN_CONST RUN_UINT_8 *runTransmit,
                                  RUN_UINT_8 *runReceive,
                                  RUN_SIZE runLength,
                                  RunSpiCompletion runCompletion,
                                  RUN_VOID *runArgument);
} RunSpiOperations;

RUN_STATIC RUN_INLINE RUN_CONST RunSpiOperations *run_spi_ops(RunDevice *runDevice)
{
    return (RUN_CONST RunSpiOperations *)run_device_operations(runDevice);
}

/*
 * Blocking full-duplex transfer. A null transmit buffer sends 0xFF and a null
 * receive buffer discards received bytes. The F103 polling backend treats a
 * timeout of zero as its short built-in watchdog rather than "do not wait".
 */
RunStatus run_spi_transfer(RunDevice *runDevice,
                           RUN_CONST RUN_UINT_8 *runTransmit,
                           RUN_UINT_8 *runReceive,
                           RUN_SIZE runLength,
                           RUN_UINT_32 runTimeoutMs);
/*
 * Accepts one asynchronous full-duplex transfer. A second transfer submitted
 * before the first completion returns RUN_STATUS_BUSY. The completion callback
 * runs only in interrupt context; buffers and the callback argument must remain
 * valid until then. It is invoked exactly once after successful acceptance.
 */
RunStatus run_spi_transfer_async(RunDevice *runDevice,
                                 RUN_CONST RUN_UINT_8 *runTransmit,
                                 RUN_UINT_8 *runReceive,
                                 RUN_SIZE runLength,
                                 RunSpiCompletion runCompletion,
                                 RUN_VOID *runArgument);
RunStatus run_spi_create_f103(RUN_UINT_32 runIndex,
                              RUN_CONST RUN_CHAR *runName,
                              RunDevice **runDeviceOut);

#endif

