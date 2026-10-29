/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - owned and borrowed string operations
 *
 * Layer: runtime
 * Purpose: Implement lengths, comparisons, allocation, append, and cleanup.
 * Dependencies: public string and memory interfaces.
 * Constraint: Growth allocates new storage before releasing old storage so an
 *             allocation failure leaves the original string intact.
 */
#include "runrtos/runtime/run_string.h"

RUN_STATIC RUN_BOOL run_string_is_valid_view(RunStringView runView)
{
    return (runView.runLength == 0U) || (runView.runData != RUN_NULL);
}

RUN_SIZE run_string_length(RUN_CONST RUN_CHAR *runText)
{
    RUN_SIZE runLength = 0U;
    if (runText == RUN_NULL)
    {
        return 0U;
    }
    while (runText[runLength] != '\0')
    {
        runLength++;
    }
    return runLength;
}

RUN_INT_32 run_string_compare_view(RunStringView runLeft, RunStringView runRight)
{
    RUN_SIZE runCount = RUN_MIN(runLeft.runLength, runRight.runLength);
    for (RUN_SIZE runIndex = 0U; runIndex < runCount; runIndex++)
    {
        RUN_UINT_8 runLeftByte = (RUN_UINT_8)runLeft.runData[runIndex];
        RUN_UINT_8 runRightByte = (RUN_UINT_8)runRight.runData[runIndex];
        if (runLeftByte != runRightByte)
        {
            return (RUN_INT_32)runLeftByte - (RUN_INT_32)runRightByte;
        }
    }
    if (runLeft.runLength == runRight.runLength)
    {
        return 0;
    }
    return (runLeft.runLength < runRight.runLength) ? -1 : 1;
}

RUN_BOOL run_string_equal_view(RunStringView runLeft, RunStringView runRight)
{
    return run_string_compare_view(runLeft, runRight) == 0;
}

RUN_STATIC RunStatus run_string_reserve(RunString *runString, RUN_SIZE runCapacity)
{
    RUN_UINT_8 *runNewData;
    RUN_SIZE runAligned = (runCapacity + 1U + RUN_MEMORY_ALIGNMENT - 1U) &
                          ~(RUN_MEMORY_ALIGNMENT - 1U);
    RunStatus runStatus;

    if (runString->runMemory == RUN_NULL)
    {
        return RUN_STATUS_NOT_INITIALIZED;
    }
    if (runCapacity <= runString->runCapacity)
    {
        return RUN_STATUS_OK;
    }
    /* Allocate first so append failure can preserve the old buffer and value. */
    runStatus = run_memory_allocate(runString->runMemory, runAligned, RUN_MEMORY_ALIGNMENT,
                                    (RUN_VOID **)&runNewData);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    if (runString->runData != RUN_NULL)
    {
        run_memory_copy(runNewData, runString->runData, runString->runLength + 1U);
        run_memory_release(runString->runMemory, runString->runData);
    }
    runString->runData = (RUN_CHAR *)runNewData;
    runString->runCapacity = runAligned - 1U;
    return RUN_STATUS_OK;
}

RunStatus run_string_from_cstr(RunMemory *runMemory,
                               RUN_CONST RUN_CHAR *runText,
                               RunString *runString)
{
    RunStringView runView;
    if (runText == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runView.runData = runText;
    runView.runLength = run_string_length(runText);
    return run_string_assign_view(runMemory, runView, runString);
}

RunStatus run_string_assign_view(RunMemory *runMemory,
                                 RunStringView runView,
                                 RunString *runString)
{
    RunStatus runStatus;
    if ((runString == RUN_NULL) || (runMemory == RUN_NULL) || !run_string_is_valid_view(runView))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    /* Assignment defines a fresh owner; the caller must deinit prior storage first. */
    runString->runMemory = runMemory;
    runString->runData = RUN_NULL;
    runString->runLength = 0U;
    runString->runCapacity = 0U;
    runStatus = run_string_reserve(runString, runView.runLength);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    if (runView.runLength != 0U)
    {
        run_memory_copy(runString->runData, runView.runData, runView.runLength);
    }
    runString->runData[runView.runLength] = '\0';
    runString->runLength = runView.runLength;
    return RUN_STATUS_OK;
}

RunStatus run_string_append_view(RunString *runString, RunStringView runView)
{
    RunStatus runStatus;
    RUN_SIZE runNewLength;
    if ((runString == RUN_NULL) || !run_string_is_valid_view(runView))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    if (runView.runLength > (RUN_SIZE_MAX - runString->runLength))
    {
        return RUN_STATUS_OVERFLOW;
    }
    runNewLength = runString->runLength + runView.runLength;
    runStatus = run_string_reserve(runString, runNewLength);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    if (runView.runLength != 0U)
    {
        run_memory_copy(runString->runData + runString->runLength,
                        runView.runData,
                        runView.runLength);
    }
    runString->runLength = runNewLength;
    runString->runData[runNewLength] = '\0';
    return RUN_STATUS_OK;
}

RunStringView run_string_as_view(RUN_CONST RunString *runString)
{
    RunStringView runView = RUN_STRING_VIEW_EMPTY;
    if (runString != RUN_NULL)
    {
        runView.runData = runString->runData;
        runView.runLength = runString->runLength;
    }
    return runView;
}

RUN_VOID run_string_deinit(RunString *runString)
{
    if ((runString != RUN_NULL) && (runString->runData != RUN_NULL))
    {
        if (runString->runMemory != RUN_NULL)
        {
            (RUN_VOID)run_memory_release(runString->runMemory, runString->runData);
        }
        runString->runData = RUN_NULL;
        runString->runLength = 0U;
        runString->runCapacity = 0U;
    }
}

