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
RunStatus run_spi_create_f103(RUN_UINT_32 runIndex,
                              RUN_CONST RUN_CHAR *runName,
                              RunDevice **runDeviceOut);

#endif

