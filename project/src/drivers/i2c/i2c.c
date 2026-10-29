/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - I2C capability forwarding
 *
 * Layer: drivers
 * Purpose: Validate transaction pointers and dispatch to an I2C backend.
 * Dependencies: public I2C/device interfaces.
 * Constraint: Empty buffers are valid; nonempty buffers must have non-NULL pointers.
 */
#include "runrtos/drivers/i2c/run_i2c.h"

RunStatus run_i2c_transaction(RunDevice *runDevice,
                              RUN_UINT_16 runAddress,
                              RUN_CONST RUN_UINT_8 *runWriteData,
                              RUN_SIZE runWriteLength,
                              RUN_UINT_8 *runReadData,
                              RUN_SIZE runReadLength,
                              RUN_UINT_32 runTimeoutMs)
{
    RUN_CONST RunI2cOperations *runOperations = run_i2c_ops(runDevice);
    if ((runOperations == RUN_NULL) || (runOperations->runTransaction == RUN_NULL) ||
        ((runWriteData == RUN_NULL) && (runWriteLength != 0U)) ||
        ((runReadData == RUN_NULL) && (runReadLength != 0U)))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    return runOperations->runTransaction(runDevice, runAddress, runWriteData,
                                         runWriteLength, runReadData,
                                         runReadLength, runTimeoutMs);
}

RunStatus run_i2c_transaction_async(RunDevice *runDevice,
                                    RUN_UINT_16 runAddress,
                                    RUN_CONST RUN_UINT_8 *runWriteData,
                                    RUN_SIZE runWriteLength,
                                    RUN_UINT_8 *runReadData,
                                    RUN_SIZE runReadLength,
                                    RunI2cCompletion runCompletion,
                                    RUN_VOID *runArgument)
{
    RUN_CONST RunI2cOperations *runOperations = run_i2c_ops(runDevice);
    if ((runOperations == RUN_NULL) ||
        (runOperations->runTransactionAsync == RUN_NULL) ||
        ((runWriteData == RUN_NULL) && (runWriteLength != 0U)) ||
        ((runReadData == RUN_NULL) && (runReadLength != 0U)) ||
        (runCompletion == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    return runOperations->runTransactionAsync(runDevice, runAddress,
                                              runWriteData, runWriteLength,
                                              runReadData, runReadLength,
                                              runCompletion, runArgument);
}

