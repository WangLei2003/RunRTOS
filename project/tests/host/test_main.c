/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - host runtime smoke tests
 *
 * Layer: tests/host
 * Purpose: Verify allocator alignment/merge integrity and owned-string formatting.
 * Dependencies: RunRTOS runtime public APIs and CTest process status.
 * Constraint: Tests use no external test framework; the first failing group is
 *             identified by its nonzero process exit code.
 */
#include "runrtos/runtime/run_format.h"
#include "runrtos/runtime/run_memory.h"
#include "runrtos/runtime/run_string.h"

RUN_STATIC RUN_INT_32 run_test_memory(void)
{
    RUN_ATTR_ALIGNED(8) RUN_UINT_8 runArena[1024];
    RunMemory *runMemory = RUN_NULL;
    RUN_VOID *runFirst = RUN_NULL;
    RUN_VOID *runSecond = RUN_NULL;
    RunMemoryStats runStats;

    if (run_memory_init(&runMemory, runArena, sizeof(runArena)) != RUN_STATUS_OK)
    {
        return 1;
    }
    if (run_memory_allocate(runMemory, 24U, RUN_MEMORY_ALIGNMENT, &runFirst) !=
        RUN_STATUS_OK)
    {
        return 2;
    }
    if (((RUN_UINTPTR_TYPE)runFirst & (RUN_MEMORY_ALIGNMENT - 1U)) != 0U)
    {
        return 3;
    }
    if (run_memory_allocate(runMemory, 64U, RUN_MEMORY_ALIGNMENT, &runSecond) !=
        RUN_STATUS_OK)
    {
        return 4;
    }
    if (run_memory_release(runMemory, runFirst) != RUN_STATUS_OK)
    {
        return 5;
    }
    if (run_memory_release(runMemory, runSecond) != RUN_STATUS_OK)
    {
        return 6;
    }
    if (run_memory_check(runMemory) != RUN_STATUS_OK)
    {
        return 7;
    }
    run_memory_get_stats(runMemory, &runStats);
    if ((runStats.runUsed != 0U) || (runStats.runFailureCount != 0U))
    {
        return 8;
    }
    return 0;
}

RUN_STATIC RUN_INT_32 run_test_strings(void)
{
    RUN_ATTR_ALIGNED(8) RUN_UINT_8 runArena[1024];
    RunMemory *runMemory = RUN_NULL;
    RunString runString;
    RunStringView runView;
    RUN_CHAR runBuffer[32];
    RUN_SIZE runLength = 0U;

    if (run_memory_init(&runMemory, runArena, sizeof(runArena)) != RUN_STATUS_OK)
    {
        return 10;
    }
    if (run_string_from_cstr(runMemory, "Run", &runString) != RUN_STATUS_OK)
    {
        return 11;
    }
    runView.runData = "RTOS";
    runView.runLength = 4U;
    if (run_string_append_view(&runString, runView) != RUN_STATUS_OK)
    {
        return 12;
    }
    runView = run_string_as_view(&runString);
    if (!run_string_equal_view(runView, (RunStringView)RUN_STRING_VIEW("RunRTOS")))
    {
        return 13;
    }
    if (run_format_buffer(runBuffer, sizeof(runBuffer), &runLength, "%s:%u",
                          runString.runData, 7U) != RUN_STATUS_OK)
    {
        return 14;
    }
    if (!run_string_equal_view(
            (RunStringView){ runBuffer, runLength },
            (RunStringView)RUN_STRING_VIEW("RunRTOS:7")))
    {
        return 15;
    }
    run_string_deinit(&runString);
    if (run_memory_check(runMemory) != RUN_STATUS_OK)
    {
        return 16;
    }
    return 0;
}

RUN_INT_32 main(RUN_VOID)
{
    RUN_INT_32 runResult = run_test_memory();
    if (runResult != 0)
    {
        return runResult;
    }
    return run_test_strings();
}

