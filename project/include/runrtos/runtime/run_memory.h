/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - bounded-time segregated memory allocator
 *
 * Layer: runtime
 * Purpose: Provide aligned allocation, release, statistics, and integrity checks.
 * Dependencies: core fixed-width types.
 * Constraint: The caller owns arena storage; allocation/release are thread-only
 *             under the kernel contract and no arbitrary alignment is promised.
 */
#ifndef RUN_MEMORY_H_
#define RUN_MEMORY_H_

#include "runrtos/core/run_types.h"

#define RUN_MEMORY_ALIGNMENT ((RUN_SIZE)8U)

/* The object is intentionally opaque; callers never define memory metadata. */
typedef RUN_STRUCT RunMemory RunMemory;

typedef RUN_STRUCT RunMemoryStats
{
    RUN_SIZE runCapacity;
    RUN_SIZE runUsed;
    RUN_SIZE runPeakUsed;
    RUN_SIZE runLargestFree;
    RUN_UINT_32 runAllocationCount;
    RUN_UINT_32 runFreeCount;
    RUN_UINT_32 runFailureCount;
} RunMemoryStats;

/*
 * Places the opaque allocator control object in the supplied arena and returns
 * its handle. The arena remains owned by the caller and is never released by
 * RunRTOS.
 */
RunStatus run_memory_init(RunMemory **runMemoryOut,
                          RUN_VOID *runArena,
                          RUN_SIZE runArenaSize);

/*
 * Allocation is thread-only, bounded by the number of size classes, and accepts
 * power-of-two alignments no larger than RUN_MEMORY_ALIGNMENT. The returned
 * payload remains valid until released exactly once. Releasing an unknown or
 * already-free payload returns INVALID_ARGUMENT.
 */
RunStatus run_memory_allocate(RunMemory *runMemory,
                              RUN_SIZE runSize,
                              RUN_SIZE runAlignment,
                              RUN_VOID **runBlockOut);
RunStatus run_memory_release(RunMemory *runMemory, RUN_VOID *runBlock);

/* Occupancy counters include physical block headers and reserved alignment. */
RUN_VOID run_memory_get_stats(RunMemory *runMemory, RunMemoryStats *runStats);

/* Walks every physical block to verify size, back-link, and magic invariants. */
RunStatus run_memory_check(RunMemory *runMemory);

/*
 * Freestanding equivalents of memcpy, memmove, memset, and memcmp.
 * run_memory_copy requires non-overlapping ranges; use run_memory_move otherwise.
 */
RUN_VOID *run_memory_copy(RUN_VOID *runDestination,
                          RUN_CONST RUN_VOID *runSource,
                          RUN_SIZE runSize);
RUN_VOID *run_memory_move(RUN_VOID *runDestination,
                          RUN_CONST RUN_VOID *runSource,
                          RUN_SIZE runSize);
RUN_VOID *run_memory_set(RUN_VOID *runDestination, RUN_INT_32 runValue, RUN_SIZE runSize);
RUN_INT_32 run_memory_compare(RUN_CONST RUN_VOID *runLeft,
                              RUN_CONST RUN_VOID *runRight,
                              RUN_SIZE runSize);

#endif

