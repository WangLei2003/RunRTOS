/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - I2C controller capability interface
 *
 * Layer: drivers
 * Purpose: Expose blocking and optional asynchronous controller transactions.
 * Dependencies: generic RunDevice framework.
 * Constraint: Addresses are unshifted 7-bit values; buffer ownership remains
 *             with the caller for the duration of the call.
 */
#ifndef RUN_I2C_H_
#define RUN_I2C_H_

#include "runrtos/drivers/run_device.h"

typedef RUN_VOID (*RunI2cCompletion)(RunDevice *runDevice,
                                     RunStatus runStatus,
                                     RUN_VOID *runArgument);

typedef RUN_STRUCT RunI2cOperations
{
    RunStatus (*runTransaction)(RunDevice *runDevice,
                                RUN_UINT_16 runAddress,
                                RUN_CONST RUN_UINT_8 *runWriteData,
                                RUN_SIZE runWriteLength,
                                RUN_UINT_8 *runReadData,
                                RUN_SIZE runReadLength,
                                RUN_UINT_32 runTimeoutMs);
    RunStatus (*runTransactionAsync)(RunDevice *runDevice,
                                     RUN_UINT_16 runAddress,
                                     RUN_CONST RUN_UINT_8 *runWriteData,
                                     RUN_SIZE runWriteLength,
                                     RUN_UINT_8 *runReadData,
                                     RUN_SIZE runReadLength,
                                     RunI2cCompletion runCompletion,
                                     RUN_VOID *runArgument);
} RunI2cOperations;

RUN_STATIC RUN_INLINE RUN_CONST RunI2cOperations *run_i2c_ops(RunDevice *runDevice)
{
    return (RUN_CONST RunI2cOperations *)run_device_operations(runDevice);
}

/*
 * Performs one addressed master transaction. A nonempty write followed by a
 * nonempty read uses a repeated START without releasing the bus. A zero timeout
 * selects the backend watchdog instead of meaning "return immediately".
 */
RunStatus run_i2c_transaction(RunDevice *runDevice,
                              RUN_UINT_16 runAddress,
                              RUN_CONST RUN_UINT_8 *runWriteData,
                              RUN_SIZE runWriteLength,
                              RUN_UINT_8 *runReadData,
                              RUN_SIZE runReadLength,
                              RUN_UINT_32 runTimeoutMs);
/*
 * Accepts one asynchronous transaction. Concurrent use of the same controller
 * returns RUN_STATUS_BUSY. The completion callback runs in interrupt context
 * after the STOP or error condition and is invoked exactly once.
 */
RunStatus run_i2c_transaction_async(RunDevice *runDevice,
                                    RUN_UINT_16 runAddress,
                                    RUN_CONST RUN_UINT_8 *runWriteData,
                                    RUN_SIZE runWriteLength,
                                    RUN_UINT_8 *runReadData,
                                    RUN_SIZE runReadLength,
                                    RunI2cCompletion runCompletion,
                                    RUN_VOID *runArgument);
RunStatus run_i2c_create_f103(RUN_UINT_32 runIndex,
                              RUN_CONST RUN_CHAR *runName,
                              RunDevice **runDeviceOut);

#endif

