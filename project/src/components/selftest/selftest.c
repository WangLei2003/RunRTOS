/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - built-in board self-test
 *
 * Layer: components/selftest
 * Purpose: Validate runtime, scheduler, timer, board I/O, and external storage.
 * Dependencies: board services and public RunRTOS APIs.
 * Constraint: Thread context only; tests restore external storage before return
 *             unless communication fails during the destructive phase.
 */
#include "runrtos/components/selftest/run_selftest.h"
#include "runrtos/core/run_config.h"
#include "runrtos/kernel/run_sync.h"
#include "runrtos/runtime/run_format.h"

typedef RUN_ENUM RunSelfTestVerdict
{
    RUN_SELFTEST_PASS = 0,
    RUN_SELFTEST_FAIL,
    RUN_SELFTEST_SKIP
} RunSelfTestVerdict;

typedef RUN_STRUCT RunSelfTestContext
{
    RunBoardDevices *runDevices;
    RunSelfTestWrite runWrite;
    RUN_VOID *runWriteContext;
    RUN_BOOL runFailed;
} RunSelfTestContext;

/* Writes one structured self-test verdict line through the output callback. */
RUN_STATIC RUN_VOID run_selftest_report(RunSelfTestContext *runContext,
                                        RUN_CONST RUN_CHAR *runName,
                                        RunSelfTestVerdict runVerdict)
{
    RUN_CONST RUN_CHAR *runText = (runVerdict == RUN_SELFTEST_PASS) ?
        "PASS" : ((runVerdict == RUN_SELFTEST_FAIL) ? "FAIL" : "SKIP");
    RUN_CHAR runLine[96];
    RUN_SIZE runLength;
    if (runVerdict == RUN_SELFTEST_FAIL)
    {
        runContext->runFailed = RUN_TRUE;
    }
    (RUN_VOID)run_format_buffer(runLine, sizeof(runLine), &runLength,
                                "[%s] %s\r\n", runText, runName);
    runContext->runWrite(runLine, runLength, runContext->runWriteContext);
}

/* Checks allocator metadata and confirms capacity covers current usage. */
RUN_STATIC RunSelfTestVerdict run_selftest_memory(
    RunSelfTestContext *runContext)
{
    RunMemoryStats runStats;
    if ((runContext->runDevices->runMemory == RUN_NULL) ||
        (run_memory_check(runContext->runDevices->runMemory) != RUN_STATUS_OK))
    {
        return RUN_SELFTEST_FAIL;
    }
    run_memory_get_stats(runContext->runDevices->runMemory, &runStats);
    return (runStats.runCapacity >= runStats.runUsed) ?
        RUN_SELFTEST_PASS : RUN_SELFTEST_FAIL;
}

/* Publishes timer completion to the timer self-test waiter. */
RUN_STATIC RUN_VOID run_selftest_timer_callback(RunTimer *runTimer,
                                                RUN_VOID *runArgument)
{
    RUN_UINT_32 *runCount = (RUN_UINT_32 *)runArgument;
    (RUN_VOID)runTimer;
    (*runCount)++;
}

/* Verifies one-shot timer scheduling and service-thread callback execution. */
RUN_STATIC RunSelfTestVerdict run_selftest_timer(RUN_VOID)
{
    RunTimer *runTimer = RUN_NULL;
    RUN_UINT_32 runCount = 0U;
    RunStatus runStatus = run_timer_create(run_selftest_timer_callback,
                                           &runCount, RUN_FALSE, 0U,
                                           &runTimer);
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_timer_start(runTimer, 2U);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        (RUN_VOID)run_thread_sleep(20U);
    }
    if (runTimer != RUN_NULL)
    {
        RunStatus runDeleteStatus = run_timer_delete(runTimer);
        if (runStatus == RUN_STATUS_OK)
        {
            runStatus = runDeleteStatus;
        }
    }
    return ((runStatus == RUN_STATUS_OK) && (runCount != 0U)) ?
        RUN_SELFTEST_PASS : RUN_SELFTEST_FAIL;
}

/* Verifies thread enumeration and a nonempty scheduler thread set. */
RUN_STATIC RunSelfTestVerdict run_selftest_thread(RUN_VOID)
{
    RunThreadInfo runThreadInfo;
    RunKernelStats runKernelStats;
    run_kernel_get_stats(&runKernelStats);
    if ((run_thread_get_info(0U, &runThreadInfo) != RUN_STATUS_OK) ||
        (runKernelStats.runThreadCount == 0U))
    {
        return RUN_SELFTEST_FAIL;
    }
    return (runThreadInfo.runStats.runStackHighWater <
            runThreadInfo.runStats.runStackSize) ?
        RUN_SELFTEST_PASS : RUN_SELFTEST_FAIL;
}

/* Verifies that the caller still has measurable stack headroom. */
RUN_STATIC RunSelfTestVerdict run_selftest_stack(RUN_VOID)
{
    RunThread *runThread = run_thread_current();
    RunThreadStats runStats;
    if ((runThread == RUN_NULL) ||
        (run_thread_get_stats(runThread, &runStats) != RUN_STATUS_OK))
    {
        return RUN_SELFTEST_FAIL;
    }
    return (runStats.runStackHighWater < runStats.runStackSize) ?
        RUN_SELFTEST_PASS : RUN_SELFTEST_FAIL;
}

/* Drives the active-low LED and reads back its output state. */
RUN_STATIC RunSelfTestVerdict run_selftest_led(
    RunSelfTestContext *runContext)
{
    RUN_BOOL runHigh;
    RunStatus runStatus;
    runStatus = run_gpio_write(runContext->runDevices->runLedPort, 0U,
                               RUN_FALSE);
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_read(runContext->runDevices->runLedPort, 0U,
                                  &runHigh);
    }
    if ((runStatus == RUN_STATUS_OK) && runHigh)
    {
        runStatus = RUN_STATUS_ERROR;
    }
    (RUN_VOID)run_gpio_write(runContext->runDevices->runLedPort, 0U, RUN_TRUE);
    return (runStatus == RUN_STATUS_OK) ?
        RUN_SELFTEST_PASS : RUN_SELFTEST_FAIL;
}

/* Checks each board key pin in its released electrical state. */
RUN_STATIC RunSelfTestVerdict run_selftest_keys(
    RunSelfTestContext *runContext)
{
    RUN_BOOL runHigh;
    RunStatus runStatus = run_gpio_read(runContext->runDevices->runGpioA, 0U,
                                        &runHigh);
    if ((runStatus != RUN_STATUS_OK) || runHigh)
    {
        return RUN_SELFTEST_FAIL;
    }
    runStatus = run_gpio_read(runContext->runDevices->runGpioC, 8U, &runHigh);
    if ((runStatus != RUN_STATUS_OK) || !runHigh)
    {
        return RUN_SELFTEST_FAIL;
    }
    runStatus = run_gpio_read(runContext->runDevices->runGpioC, 9U, &runHigh);
    if ((runStatus != RUN_STATUS_OK) || !runHigh)
    {
        return RUN_SELFTEST_FAIL;
    }
    runStatus = run_gpio_read(runContext->runDevices->runGpioD, 2U, &runHigh);
    return ((runStatus == RUN_STATUS_OK) && runHigh) ?
        RUN_SELFTEST_PASS : RUN_SELFTEST_FAIL;
}

/* Confirms the console UART is present and its transmitter drains. */
RUN_STATIC RunSelfTestVerdict run_selftest_uart(
    RunSelfTestContext *runContext)
{
    if ((runContext->runDevices->runUart1 == RUN_NULL) ||
        (run_device_class(runContext->runDevices->runUart1) !=
         RUN_DEVICE_CLASS_UART))
    {
        return RUN_SELFTEST_FAIL;
    }
    return (run_uart_flush(runContext->runDevices->runUart1, 100U) ==
            RUN_STATUS_OK) ? RUN_SELFTEST_PASS : RUN_SELFTEST_FAIL;
}

/* Reversibly checks one 24C02 byte and restores the original value. */
RUN_STATIC RunSelfTestVerdict run_selftest_eeprom(
    RunSelfTestContext *runContext)
{
    RUN_UINT_8 runOriginal;
    RUN_UINT_8 runObserved;
    RUN_UINT_8 runTestValue;
    RunStatus runStatus;
    RunStatus runRestoreStatus;

    runStatus = run_board_24c02_read_byte(runContext->runDevices, 0U,
                                         &runOriginal);
    if (runStatus != RUN_STATUS_OK)
    {
        return RUN_SELFTEST_FAIL;
    }
    runTestValue = (RUN_UINT_8)(runOriginal ^ 0xA5U);
    runStatus = run_board_24c02_write_byte(runContext->runDevices, 0U,
                                           runTestValue);
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_board_24c02_read_byte(runContext->runDevices, 0U,
                                              &runObserved);
    }
    if ((runStatus == RUN_STATUS_OK) && (runObserved != runTestValue))
    {
        runStatus = RUN_STATUS_ERROR;
    }
    runRestoreStatus = run_board_24c02_write_byte(runContext->runDevices, 0U,
                                                  runOriginal);
    if (runRestoreStatus == RUN_STATUS_OK)
    {
        runRestoreStatus = run_board_24c02_read_byte(
            runContext->runDevices, 0U, &runObserved);
        if ((runRestoreStatus == RUN_STATUS_OK) &&
            (runObserved != runOriginal))
        {
            runRestoreStatus = RUN_STATUS_ERROR;
        }
    }
    return ((runStatus == RUN_STATUS_OK) &&
            (runRestoreStatus == RUN_STATUS_OK)) ?
        RUN_SELFTEST_PASS : RUN_SELFTEST_FAIL;
}

/* Executes all board and kernel checks and reports every verdict. */
RunStatus run_selftest_execute(RunBoardDevices *runDevices,
                               RunSelfTestWrite runWrite,
                               RUN_VOID *runWriteContext)
{
    RunSelfTestContext runContext;
    RunStatus runFlashStatus;
    if ((runDevices == RUN_NULL) || (runWrite == RUN_NULL) ||
        run_kernel_is_in_isr())
    {
        return ((runDevices == RUN_NULL) || (runWrite == RUN_NULL)) ?
            RUN_STATUS_INVALID_ARGUMENT : RUN_STATUS_CONTEXT;
    }
    runContext.runDevices = runDevices;
    runContext.runWrite = runWrite;
    runContext.runWriteContext = runWriteContext;
    runContext.runFailed = RUN_FALSE;

    run_selftest_report(&runContext, "memory",
                        run_selftest_memory(&runContext));
    run_selftest_report(&runContext, "thread", run_selftest_thread());
    run_selftest_report(&runContext, "timer", run_selftest_timer());
    run_selftest_report(&runContext, "stack-watermark", run_selftest_stack());
    run_selftest_report(&runContext, "led", run_selftest_led(&runContext));
    run_selftest_report(&runContext, "keys", run_selftest_keys(&runContext));
    run_selftest_report(&runContext, "uart", run_selftest_uart(&runContext));
    run_selftest_report(&runContext, "24c02", run_selftest_eeprom(&runContext));
    runFlashStatus = run_board_w25q16_selftest(runDevices);
    run_selftest_report(&runContext, "w25q16",
                        (runFlashStatus == RUN_STATUS_OK) ?
                            RUN_SELFTEST_PASS :
                            ((runFlashStatus == RUN_STATUS_NO_MEMORY) ?
                             RUN_SELFTEST_SKIP : RUN_SELFTEST_FAIL));
    return runContext.runFailed ? RUN_STATUS_ERROR : RUN_STATUS_OK;
}
