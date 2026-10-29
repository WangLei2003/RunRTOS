/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - SPI capability forwarding
 *
 * Layer: drivers
 * Purpose: Validate full-duplex arguments and dispatch to SPI operations.
 * Dependencies: public SPI/device interfaces.
 * Constraint: At least one direction may be absent, but not both for a nonempty transfer.
 */
#include "runrtos/drivers/spi/run_spi.h"

/* Forwards a validated blocking full-duplex transfer to the SPI backend. */
RunStatus run_spi_transfer(RunDevice *runDevice,
                           RUN_CONST RUN_UINT_8 *runTransmit,
                           RUN_UINT_8 *runReceive,
                           RUN_SIZE runLength,
                           RUN_UINT_32 runTimeoutMs)
{
    RUN_CONST RunSpiOperations *runOperations = run_spi_ops(runDevice);
    if ((runOperations == RUN_NULL) || (runOperations->runTransfer == RUN_NULL) ||
        ((runTransmit == RUN_NULL) && (runReceive == RUN_NULL) &&
         (runLength != 0U)))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    return runOperations->runTransfer(runDevice, runTransmit, runReceive,
                                      runLength, runTimeoutMs);
}

/* Forwards one asynchronous full-duplex transfer and its completion callback. */
RunStatus run_spi_transfer_async(RunDevice *runDevice,
                                 RUN_CONST RUN_UINT_8 *runTransmit,
                                 RUN_UINT_8 *runReceive,
                                 RUN_SIZE runLength,
                                 RunSpiCompletion runCompletion,
                                 RUN_VOID *runArgument)
{
    RUN_CONST RunSpiOperations *runOperations = run_spi_ops(runDevice);
    if ((runOperations == RUN_NULL) ||
        (runOperations->runTransferAsync == RUN_NULL) ||
        ((runTransmit == RUN_NULL) && (runReceive == RUN_NULL) &&
         (runLength != 0U)) ||
        (runCompletion == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    return runOperations->runTransferAsync(runDevice, runTransmit, runReceive,
                                           runLength, runCompletion,
                                           runArgument);
}

