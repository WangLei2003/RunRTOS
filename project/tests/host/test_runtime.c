/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - runtime allocator, string, and format tests
 *
 * Layer: tests/host
 * Purpose: Stress allocator invariants and exercise bounded runtime boundaries.
 * Dependencies: host assertions and public runtime APIs.
 * Constraint: Random allocation uses a fixed LCG seed for reproducibility.
 */
#include "test_harness.h"
#include "runrtos/runtime/run_format.h"
#include "runrtos/runtime/run_memory.h"
#include "runrtos/runtime/run_string.h"

#define RUN_TEST_RUNTIME_BLOCKS 96U

/* Exercises allocator alignment, OOM, fragmentation, and random allocation. */
RUN_STATIC RUN_VOID run_test_allocator_runtime(RunTestContext *runContext)
{
    RUN_ATTR_ALIGNED(8) RUN_UINT_8 runArena[4096];
    RunMemory *runMemory = RUN_NULL;
    RUN_VOID *runBlocks[RUN_TEST_RUNTIME_BLOCKS];
    RunMemoryStats runStats;
    RUN_UINT_32 runRandom = 0x13579BDFU;
    RUN_UINT_32 runLive = 0U;
    RUN_VOID *runProbe = RUN_NULL;
    RUN_SIZE runIndex;

    run_test_suite_begin(runContext, "runtime-allocator");
    RUN_TEST_STATUS(runContext,
        run_memory_init(&runMemory, runArena, sizeof(runArena)), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
        run_memory_allocate(runMemory, 24U, 8U, &runBlocks[0]), RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext,
        (((RUN_UINTPTR_TYPE)runBlocks[0] & 7U) == 0U));
    RUN_TEST_STATUS(runContext,
        run_memory_release(runMemory, runBlocks[0]), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext, run_memory_check(runMemory), RUN_STATUS_OK);

    /* OOM must leave the arena structurally intact and account the failure. */
    RUN_TEST_STATUS(runContext,
        run_memory_allocate(runMemory, sizeof(runArena), 8U, &runProbe),
        RUN_STATUS_NO_MEMORY);
    RUN_TEST_STATUS(runContext, run_memory_check(runMemory), RUN_STATUS_OK);

    for (runIndex = 0U; runIndex < RUN_TEST_RUNTIME_BLOCKS; runIndex++)
    {
        runBlocks[runIndex] = RUN_NULL;
    }
    for (runIndex = 0U; runIndex < 1024U; runIndex++)
    {
        RUN_UINT_32 runSlot;
        runRandom = runRandom * 1664525U + 1013904223U;
        runSlot = (runRandom >> 8U) % RUN_TEST_RUNTIME_BLOCKS;
        if (runBlocks[runSlot] == RUN_NULL)
        {
            RUN_SIZE runSize = 8U + ((runRandom >> 16U) % 96U);
            RunStatus runStatus = run_memory_allocate(runMemory, runSize, 8U,
                                                      &runBlocks[runSlot]);
            RUN_TEST_CHECK(runContext,
                (runStatus == RUN_STATUS_OK) ||
                (runStatus == RUN_STATUS_NO_MEMORY));
            if (runStatus == RUN_STATUS_OK)
            {
                runLive++;
                RUN_TEST_CHECK(runContext,
                    (((RUN_UINTPTR_TYPE)runBlocks[runSlot] & 7U) == 0U));
            }
        }
        else
        {
            RUN_TEST_STATUS(runContext,
                run_memory_release(runMemory, runBlocks[runSlot]), RUN_STATUS_OK);
            runBlocks[runSlot] = RUN_NULL;
            runLive--;
        }
        if ((runIndex & 31U) == 0U)
        {
            RUN_TEST_STATUS(runContext, run_memory_check(runMemory), RUN_STATUS_OK);
        }
    }
    for (runIndex = 0U; runIndex < RUN_TEST_RUNTIME_BLOCKS; runIndex++)
    {
        if (runBlocks[runIndex] != RUN_NULL)
        {
            RUN_TEST_STATUS(runContext,
                run_memory_release(runMemory, runBlocks[runIndex]), RUN_STATUS_OK);
            runLive--;
        }
    }
    RUN_TEST_CHECK(runContext, runLive == 0U);
    RUN_TEST_STATUS(runContext, run_memory_check(runMemory), RUN_STATUS_OK);
    run_memory_get_stats(runMemory, &runStats);
    RUN_TEST_CHECK(runContext, runStats.runUsed == 0U);
    RUN_TEST_CHECK(runContext, runStats.runFailureCount != 0U);
}

/* Exercises string ownership and bounded formatting/parsing boundaries. */
RUN_STATIC RUN_VOID run_test_strings_runtime(RunTestContext *runContext)
{
    RUN_ATTR_ALIGNED(8) RUN_UINT_8 runArena[1024];
    RunMemory *runMemory = RUN_NULL;
    RunString runString;
    RunStringView runView;
    RUN_CHAR runBuffer[12];
    RUN_SIZE runLength = 99U;
    RUN_UINT_32 runValue = 0U;
    RUN_BOOL runValid = RUN_TRUE;

    run_test_suite_begin(runContext, "runtime-string-format");
    RUN_TEST_STATUS(runContext,
        run_memory_init(&runMemory, runArena, sizeof(runArena)), RUN_STATUS_OK);
    RUN_TEST_STATUS(runContext,
        run_string_from_cstr(runMemory, "Run", &runString), RUN_STATUS_OK);
    runView.runData = "RTOS";
    runView.runLength = 4U;
    RUN_TEST_STATUS(runContext,
        run_string_append_view(&runString, runView), RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext,
        run_string_equal_view(run_string_as_view(&runString),
                              (RunStringView)RUN_STRING_VIEW("RunRTOS")));
    RUN_TEST_CHECK(runContext,
        run_string_compare_view((RunStringView)RUN_STRING_VIEW("a"),
                                (RunStringView)RUN_STRING_VIEW("b")) < 0);
    RUN_TEST_CHECK(runContext,
        !run_string_equal_view((RunStringView)RUN_STRING_VIEW("ab"),
                               (RunStringView)RUN_STRING_VIEW("a")));

    RUN_TEST_STATUS(runContext,
        run_format_buffer(runBuffer, sizeof(runBuffer), &runLength, "%s:%u",
                          "RunRunRun", 42U), RUN_STATUS_OVERFLOW);
    RUN_TEST_CHECK(runContext, runLength < sizeof(runBuffer));
    RUN_TEST_CHECK(runContext, runBuffer[runLength] == '\0');
    RUN_TEST_STATUS(runContext,
        run_format_buffer(runBuffer, sizeof(runBuffer), &runLength, "%08x", 0x2AU),
        RUN_STATUS_OK);
    RUN_TEST_CHECK(runContext,
        run_string_equal_view((RunStringView){ runBuffer, runLength },
                              (RunStringView)RUN_STRING_VIEW("0000002a")));
    RUN_TEST_CHECK(runContext,
        run_format_parse_u32("4294967295", &runValue));
    RUN_TEST_CHECK(runContext, runValue == 0xFFFFFFFFU);
    RUN_TEST_CHECK(runContext, !run_format_parse_u32("12x", &runValue));
    RUN_TEST_CHECK(runContext,
        !run_format_parse_u32("4294967296", &runValue));
    RUN_TEST_CHECK(runContext,
        run_format_parse_u64("18446744073709551615", &runValid) ==
        0xFFFFFFFFFFFFFFFFULL);
    RUN_TEST_CHECK(runContext, runValid);

    run_string_deinit(&runString);
    RUN_TEST_STATUS(runContext, run_memory_check(runMemory), RUN_STATUS_OK);
}

/* Runs all runtime allocator, string, and format tests. */
RUN_VOID run_test_runtime_suite(RunTestContext *runContext)
{
    run_test_allocator_runtime(runContext);
    run_test_strings_runtime(runContext);
}
