/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - generic driver dispatch and async contract tests
 *
 * Layer: tests/host
 * Purpose: Validate capability forwarding, busy rejection, completion count,
 *          and error propagation without hardware register access.
 * Dependencies: public device/UART/SPI/I2C APIs and host assertions.
 * Constraint: Mock completion is invoked explicitly to model an ISR boundary.
 */
#include "test_harness.h"
#include "runrtos/drivers/i2c/run_i2c.h"
#include "runrtos/drivers/spi/run_spi.h"
#include "runrtos/drivers/uart/run_uart.h"

typedef RUN_STRUCT RunMockTransport
{
    RUN_BOOL runBusy;
    RUN_UINT_32 runCompletionCount;
    RunStatus runCompletionStatus;
    RUN_VOID *runCompletionArgument;
} RunMockTransport;

RUN_STATIC RunStatus RunMockUartStatus;
RUN_STATIC RUN_SIZE RunMockUartTransferred;
RUN_STATIC RUN_VOID *RunMockUartArgument;
RUN_STATIC RunStatus RunMockSpiStatus;
RUN_STATIC RUN_VOID *RunMockSpiArgument;
RUN_STATIC RunStatus RunMockI2cStatus;
RUN_STATIC RUN_VOID *RunMockI2cArgument;

/* Records UART completion data for generic driver dispatch assertions. */
RUN_STATIC RUN_VOID run_mock_uart_completion(RunDevice *runDevice,
                                             RunStatus runStatus,
                                             RUN_SIZE runTransferred,
                                             RUN_VOID *runArgument)
{
    (RUN_VOID)runDevice;
    RunMockUartStatus = runStatus;
    RunMockUartTransferred = runTransferred;
    RunMockUartArgument = runArgument;
}

/* Records SPI completion data for generic driver dispatch assertions. */
RUN_STATIC RUN_VOID run_mock_spi_completion(RunDevice *runDevice,
                                            RunStatus runStatus,
                                            RUN_VOID *runArgument)
{
    (RUN_VOID)runDevice;
    RunMockSpiStatus = runStatus;
    RunMockSpiArgument = runArgument;
}

/* Records I2C completion data for generic driver dispatch assertions. */
RUN_STATIC RUN_VOID run_mock_i2c_completion(RunDevice *runDevice,
                                            RunStatus runStatus,
                                            RUN_VOID *runArgument)
{
    (RUN_VOID)runDevice;
    RunMockI2cStatus = runStatus;
    RunMockI2cArgument = runArgument;
}

/* Validates the canonical 115200 8N1 UART configuration. */
RUN_STATIC RunStatus run_mock_uart_configure(RunDevice *runDevice,
                                             RUN_UINT_32 runBaudRate,
                                             RUN_UINT_8 runDataBits,
                                             RUN_UINT_8 runParity,
                                             RUN_UINT_8 runStopBits)
{
    (RUN_VOID)runDevice;
    return ((runBaudRate == 115200U) && (runDataBits == 8U) &&
            (runParity == 0U) && (runStopBits == 1U)) ?
        RUN_STATUS_OK : RUN_STATUS_INVALID_ARGUMENT;
}

/* Returns immediate success for the mock blocking UART write. */
RUN_STATIC RunStatus run_mock_uart_write(RunDevice *runDevice,
                                         RUN_CONST RUN_UINT_8 *runData,
                                         RUN_SIZE runLength,
                                         RunTimeMs runTimeout)
{
    (RUN_VOID)runDevice;
    (RUN_VOID)runData;
    (RUN_VOID)runLength;
    (RUN_VOID)runTimeout;
    return RUN_STATUS_OK;
}

/* Claims the mock UART transmitter and prepares deferred completion state. */
RUN_STATIC RunStatus run_mock_uart_write_async(RunDevice *runDevice,
                                               RUN_CONST RUN_UINT_8 *runData,
                                               RUN_SIZE runLength,
                                               RunUartCompletion runCompletion,
                                               RUN_VOID *runArgument)
{
    RunMockTransport *runMock =
        (RunMockTransport *)run_device_context(runDevice);
    (RUN_VOID)runData;
    (RUN_VOID)runLength;
    if (runMock->runBusy)
    {
        return RUN_STATUS_BUSY;
    }
    runMock->runBusy = RUN_TRUE;
    runMock->runCompletionStatus = RUN_STATUS_TIMEOUT;
    runMock->runCompletionArgument = runArgument;
    RunMockTransport *runCompletionContext = runMock;
    /* Completion is deferred to the test's simulated ISR. */
    RunMockUartStatus = runCompletionContext->runCompletionStatus;
    (RUN_VOID)runCompletion;
    return RUN_STATUS_OK;
}

/* Models an empty UART receiver with a timeout result. */
RUN_STATIC RunStatus run_mock_uart_read(RunDevice *runDevice,
                                        RUN_UINT_8 *runData,
                                        RUN_SIZE runLength,
                                        RUN_SIZE *runReadOut,
                                        RunTimeMs runTimeout)
{
    (RUN_VOID)runDevice;
    (RUN_VOID)runData;
    (RUN_VOID)runLength;
    (RUN_VOID)runTimeout;
    *runReadOut = 0U;
    return RUN_STATUS_TIMEOUT;
}

/* Returns immediate success for the mock UART flush. */
RUN_STATIC RunStatus run_mock_uart_flush(RunDevice *runDevice,
                                         RunTimeMs runTimeout)
{
    (RUN_VOID)runDevice;
    (RUN_VOID)runTimeout;
    return RUN_STATUS_OK;
}

/* Returns immediate success for the mock blocking SPI transfer. */
RUN_STATIC RunStatus run_mock_spi_transfer(RunDevice *runDevice,
                                           RUN_CONST RUN_UINT_8 *runTransmit,
                                           RUN_UINT_8 *runReceive,
                                           RUN_SIZE runLength,
                                           RUN_UINT_32 runTimeoutMs)
{
    (RUN_VOID)runDevice;
    (RUN_VOID)runTransmit;
    (RUN_VOID)runReceive;
    (RUN_VOID)runLength;
    (RUN_VOID)runTimeoutMs;
    return RUN_STATUS_OK;
}

/* Claims the mock SPI controller and prepares a deferred error result. */
RUN_STATIC RunStatus run_mock_spi_transfer_async(
    RunDevice *runDevice,
    RUN_CONST RUN_UINT_8 *runTransmit,
    RUN_UINT_8 *runReceive,
    RUN_SIZE runLength,
    RunSpiCompletion runCompletion,
    RUN_VOID *runArgument)
{
    RunMockTransport *runMock =
        (RunMockTransport *)run_device_context(runDevice);
    (RUN_VOID)runTransmit;
    (RUN_VOID)runReceive;
    (RUN_VOID)runLength;
    (RUN_VOID)runCompletion;
    if (runMock->runBusy)
    {
        return RUN_STATUS_BUSY;
    }
    runMock->runBusy = RUN_TRUE;
    runMock->runCompletionStatus = RUN_STATUS_BUSY;
    runMock->runCompletionArgument = runArgument;
    return RUN_STATUS_OK;
}

/* Returns immediate success for the mock blocking I2C transaction. */
RUN_STATIC RunStatus run_mock_i2c_transaction(RunDevice *runDevice,
                                              RUN_UINT_16 runAddress,
                                              RUN_CONST RUN_UINT_8 *runWriteData,
                                              RUN_SIZE runWriteLength,
                                              RUN_UINT_8 *runReadData,
                                              RUN_SIZE runReadLength,
                                              RUN_UINT_32 runTimeoutMs)
{
    (RUN_VOID)runDevice;
    (RUN_VOID)runAddress;
    (RUN_VOID)runWriteData;
    (RUN_VOID)runWriteLength;
    (RUN_VOID)runReadData;
    (RUN_VOID)runReadLength;
    (RUN_VOID)runTimeoutMs;
    return RUN_STATUS_OK;
}

/* Claims the mock I2C controller and prepares a deferred completion. */
RUN_STATIC RunStatus run_mock_i2c_transaction_async(
    RunDevice *runDevice,
    RUN_UINT_16 runAddress,
    RUN_CONST RUN_UINT_8 *runWriteData,
    RUN_SIZE runWriteLength,
    RUN_UINT_8 *runReadData,
    RUN_SIZE runReadLength,
    RunI2cCompletion runCompletion,
    RUN_VOID *runArgument)
{
    RunMockTransport *runMock =
        (RunMockTransport *)run_device_context(runDevice);
    (RUN_VOID)runAddress;
    (RUN_VOID)runWriteData;
    (RUN_VOID)runWriteLength;
    (RUN_VOID)runReadData;
    (RUN_VOID)runReadLength;
    (RUN_VOID)runCompletion;
    if (runMock->runBusy)
    {
        return RUN_STATUS_BUSY;
    }
    runMock->runBusy = RUN_TRUE;
    runMock->runCompletionStatus = RUN_STATUS_ERROR;
    runMock->runCompletionArgument = runArgument;
    return RUN_STATUS_OK;
}

RUN_STATIC RUN_CONST RunUartOperations RunMockUartOperations = {
    run_mock_uart_configure,
    run_mock_uart_write,
    run_mock_uart_write_async,
    run_mock_uart_read,
    run_mock_uart_flush
};

RUN_STATIC RUN_CONST RunSpiOperations RunMockSpiOperations = {
    run_mock_spi_transfer,
    run_mock_spi_transfer_async
};

RUN_STATIC RUN_CONST RunI2cOperations RunMockI2cOperations = {
    run_mock_i2c_transaction,
    run_mock_i2c_transaction_async
};

/* Executes driver forwarding, ownership, and completion contract tests. */
RUN_VOID run_test_driver_suite(RunTestContext *runContext)
{
    RunDeviceConfig runConfig;
    RunDevice *runDevice = RUN_NULL;
    RunMockTransport *runMock;
    RUN_UINT_32 runArgument = 0x55U;
    RUN_UINT_8 runData[2] = { 1U, 2U };
    RUN_SIZE runRead = 0U;

    run_test_suite_begin(runContext, "drivers");
    runConfig.runName = "mock-uart";
    runConfig.runClass = RUN_DEVICE_CLASS_UART;
    runConfig.runOperations = &RunMockUartOperations;
    runConfig.runContextSize = sizeof(RunMockTransport);
    RUN_TEST_STATUS(runContext, run_device_create(&runConfig, &runDevice),
                    RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext,
        run_string_equal_view(
            (RunStringView){ run_device_name(runDevice),
                             run_string_length(run_device_name(runDevice)) },
            (RunStringView)RUN_STRING_VIEW("mock-uart")));
    runMock = (RunMockTransport *)run_device_context(runDevice);
    RUN_TEST_STATUS(runContext,
        run_uart_configure(runDevice, 115200U, 8U, 0U, 1U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
        run_uart_write(runDevice, runData, sizeof(runData), 10U), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
        run_uart_read(runDevice, runData, sizeof(runData), &runRead, 1U),
        RUN_STATUS_TIMEOUT);
    RUN_TEST_CHECK(runContext, runRead == 0U);
    RUN_TEST_STATUS(runContext,
        run_uart_write_async(runDevice, runData, sizeof(runData),
                             run_mock_uart_completion, &runArgument),
        RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
        run_uart_write_async(runDevice, runData, sizeof(runData),
                             run_mock_uart_completion, &runArgument),
        RUN_STATUS_BUSY);
    runMock->runCompletionCount++;
    RunMockUartStatus = runMock->runCompletionStatus;
    RunMockUartTransferred = sizeof(runData);
    RunMockUartArgument = runMock->runCompletionArgument;
    runMock->runBusy = RUN_FALSE;
    RUN_TEST_CHECK(runContext, runMock->runCompletionCount == 1U);
    RUN_TEST_STATUS(runContext, RunMockUartStatus, RUN_STATUS_TIMEOUT);
    RUN_TEST_CHECK(runContext, RunMockUartTransferred == sizeof(runData));
    RUN_TEST_CHECK(runContext, RunMockUartArgument == &runArgument);
    RUN_TEST_STATUS(runContext, run_device_delete(runDevice), RUN_STATUS_OK);

    runConfig.runName = "mock-spi";
    runConfig.runClass = RUN_DEVICE_CLASS_SPI;
    runConfig.runOperations = &RunMockSpiOperations;
    RUN_TEST_STATUS(runContext, run_device_create(&runConfig, &runDevice),
                    RUN_STATUS_OK);
    runMock = (RunMockTransport *)run_device_context(runDevice);
    RUN_TEST_STATUS(runContext,
        run_spi_transfer_async(runDevice, runData, runData, sizeof(runData),
                               run_mock_spi_completion, &runArgument),
        RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
        run_spi_transfer_async(runDevice, runData, runData, sizeof(runData),
                               run_mock_spi_completion, &runArgument),
        RUN_STATUS_BUSY);
    RunMockSpiStatus = runMock->runCompletionStatus;
    RunMockSpiArgument = runMock->runCompletionArgument;
    RUN_TEST_STATUS(runContext, RunMockSpiStatus, RUN_STATUS_BUSY);
    RUN_TEST_CHECK(runContext, RunMockSpiArgument == &runArgument);
    RUN_TEST_STATUS(runContext, run_device_delete(runDevice), RUN_STATUS_OK);

    runConfig.runName = "mock-i2c";
    runConfig.runClass = RUN_DEVICE_CLASS_I2C;
    runConfig.runOperations = &RunMockI2cOperations;
    RUN_TEST_STATUS(runContext, run_device_create(&runConfig, &runDevice),
                    RUN_STATUS_OK);
    runMock = (RunMockTransport *)run_device_context(runDevice);
    RUN_TEST_STATUS(runContext,
        run_i2c_transaction_async(runDevice, 0x50U, runData, 1U, runData, 1U,
                                  run_mock_i2c_completion, &runArgument),
        RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
        run_i2c_transaction_async(runDevice, 0x50U, runData, 1U, runData, 1U,
                                  run_mock_i2c_completion, &runArgument),
        RUN_STATUS_BUSY);
    RunMockI2cStatus = runMock->runCompletionStatus;
    RunMockI2cArgument = runMock->runCompletionArgument;
    RUN_TEST_STATUS(runContext, RunMockI2cStatus, RUN_STATUS_ERROR);
    RUN_TEST_CHECK(runContext, RunMockI2cArgument == &runArgument);
    RUN_TEST_STATUS(runContext, run_device_delete(runDevice), RUN_STATUS_OK);
}
