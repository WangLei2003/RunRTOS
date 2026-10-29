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
 * Performs one addressed master transaction. The F103 implementation currently
 * supports either a write or a read, not a repeated-start write-then-read; that
 * combination returns RUN_STATUS_UNSUPPORTED. A zero timeout selects the backend
 * watchdog instead of meaning "return immediately".
 */
RunStatus run_i2c_transaction(RunDevice *runDevice,
                              RUN_UINT_16 runAddress,
                              RUN_CONST RUN_UINT_8 *runWriteData,
                              RUN_SIZE runWriteLength,
                              RUN_UINT_8 *runReadData,
                              RUN_SIZE runReadLength,
                              RUN_UINT_32 runTimeoutMs);
RunStatus run_i2c_create_f103(RUN_UINT_32 runIndex,
                              RUN_CONST RUN_CHAR *runName,
                              RunDevice **runDeviceOut);

#endif

