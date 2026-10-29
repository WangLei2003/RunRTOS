/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - segregated-fit allocator
 *
 * Layer: runtime
 * Purpose: Allocate and coalesce 8-byte-aligned blocks from a caller-owned arena.
 * Dependencies: private block layout and public memory contract.
 * Constraint: Free blocks are indexed by floor(log2(size)); allocation searches
 *             only equal-or-larger classes and immediately merges physical neighbors.
 */
#include "run_memory_internal.h"

#define RUN_MEMORY_CLASS_COUNT 32U

/* Rounds an address or size upward to the requested power-of-two boundary. */
RUN_STATIC RUN_SIZE run_memory_align_up(RUN_SIZE runValue, RUN_SIZE runAlignment)
{
    /* Alignment is always a power of two, so mask low bits instead of dividing. */
    return (runValue + (runAlignment - (RUN_SIZE)1U)) & ~(runAlignment - (RUN_SIZE)1U);
}

/* Maps a byte size to its floor(log2) free-list class. */
RUN_STATIC RUN_UINT_32 run_memory_class(RUN_SIZE runSize)
{
    RUN_UINT_32 runClass = 0U;
    while ((runSize > (RUN_SIZE)1U) && (runClass < (RUN_MEMORY_CLASS_COUNT - 1U)))
    {
        runSize >>= 1U;
        runClass++;
    }
    return runClass;
}

/* Returns the intrusive links stored in a free block's payload. */
RUN_STATIC RunMemoryFreeLinks *run_memory_links(RunMemoryBlock *runBlock)
{
    /* Reuse the payload of a free block for intrusive list links. */
    return (RunMemoryFreeLinks *)(RUN_VOID *)((RUN_UINT_8 *)runBlock + sizeof(RunMemoryBlock));
}

/* Publishes a free block into its size class and updates the class bitmap. */
RUN_STATIC RUN_VOID run_memory_insert(RunMemory *runMemory, RunMemoryBlock *runBlock)
{
    RUN_UINT_32 runClass = run_memory_class(runBlock->runSize);
    RunMemoryFreeLinks *runLinks = run_memory_links(runBlock);
    runLinks->runPrevious = RUN_NULL;
    runLinks->runNext = runMemory->runFreeHeads[runClass];
    if (runMemory->runFreeHeads[runClass] != RUN_NULL)
    {
        run_memory_links(runMemory->runFreeHeads[runClass])->runPrevious = runBlock;
    }
    runMemory->runFreeHeads[runClass] = runBlock;
    runMemory->runFreeBitmap |= (1UL << runClass);
    runBlock->runMagic = RUN_MEMORY_BLOCK_FREE;
}

/* Unlinks a free block and clears an empty size class from the bitmap. */
RUN_STATIC RUN_VOID run_memory_remove(RunMemory *runMemory, RunMemoryBlock *runBlock)
{
    RUN_UINT_32 runClass = run_memory_class(runBlock->runSize);
    RunMemoryFreeLinks *runLinks = run_memory_links(runBlock);
    if (runLinks->runPrevious != RUN_NULL)
    {
        run_memory_links(runLinks->runPrevious)->runNext = runLinks->runNext;
    }
    else
    {
        runMemory->runFreeHeads[runClass] = runLinks->runNext;
    }
    if (runLinks->runNext != RUN_NULL)
    {
        run_memory_links(runLinks->runNext)->runPrevious = runLinks->runPrevious;
    }
    if (runMemory->runFreeHeads[runClass] == RUN_NULL)
    {
        runMemory->runFreeBitmap &= ~(1UL << runClass);
    }
    runLinks->runNext = RUN_NULL;
    runLinks->runPrevious = RUN_NULL;
}

/* Finds the first sufficiently large block in ascending size classes. */
RUN_STATIC RunMemoryBlock *run_memory_find(RunMemory *runMemory, RUN_SIZE runNeed)
{
    RUN_UINT_32 runClass = run_memory_class(runNeed);
    /* Ignore smaller classes in one operation, then inspect set classes in order. */
    RUN_UINT_32 runCandidates = runMemory->runFreeBitmap & (~0UL << runClass);

    while (runCandidates != 0U)
    {
        runClass = (RUN_UINT_32)__builtin_ctz(runCandidates);
        RunMemoryBlock *runBlock = runMemory->runFreeHeads[runClass];
        while (runBlock != RUN_NULL)
        {
            if (runBlock->runSize >= runNeed)
            {
                return runBlock;
            }
            runBlock = run_memory_links(runBlock)->runNext;
        }
        /* Clear the least-significant set class and continue with larger classes. */
        runCandidates &= runCandidates - 1U;
    }
    return RUN_NULL;
}

/* Initializes the allocator metadata and one arena-spanning free block. */
RunStatus run_memory_init(RunMemory **runMemoryOut,
                          RUN_VOID *runArena,
                          RUN_SIZE runArenaSize)
{
    RUN_SIZE runAddress;
    RUN_SIZE runAlignedArena;
    RUN_SIZE runAlignedEnd;
    RUN_SIZE runBlockStart;
    RunMemory *runMemory;

    if ((runMemoryOut == RUN_NULL) || (runArena == RUN_NULL) ||
        (runArenaSize < (sizeof(RunMemoryBlock) + RUN_MEMORY_BLOCK_MIN_PAYLOAD)))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    *runMemoryOut = RUN_NULL;

    /* The caller's arena may begin unaligned and may include trailing pad bytes. */
    runAddress = (RUN_SIZE)runArena;
    runAlignedArena = run_memory_align_up(runAddress, RUN_MEMORY_ALIGNMENT);
    runAlignedEnd = (runAddress + runArenaSize) & ~(RUN_MEMORY_ALIGNMENT - (RUN_SIZE)1U);
    runBlockStart = run_memory_align_up(runAlignedArena + sizeof(RunMemory),
                                        RUN_MEMORY_ALIGNMENT);
    if ((runAlignedEnd <= runBlockStart) ||
        ((runAlignedEnd - runBlockStart) <
         (sizeof(RunMemoryBlock) + RUN_MEMORY_BLOCK_MIN_PAYLOAD)))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }

    runMemory = (RunMemory *)(RUN_VOID *)runAlignedArena;
    runMemory->runFreeBitmap = 0U;
    runMemory->runCapacity = 0U;
    runMemory->runUsed = 0U;
    runMemory->runPeakUsed = 0U;
    runMemory->runAllocationCount = 0U;
    runMemory->runFreeCount = 0U;
    runMemory->runFailureCount = 0U;
    for (RUN_UINT_32 runIndex = 0U; runIndex < RUN_MEMORY_CLASS_COUNT; runIndex++)
    {
        runMemory->runFreeHeads[runIndex] = RUN_NULL;
    }

    runMemory->runFirstBlock = (RunMemoryBlock *)(RUN_VOID *)runBlockStart;
    runMemory->runFirstBlock->runSize = runAlignedEnd - runBlockStart;
    runMemory->runFirstBlock->runPreviousPhysical = RUN_NULL;
    runMemory->runCapacity = runAlignedEnd - runBlockStart;
    run_memory_insert(runMemory, runMemory->runFirstBlock);
    *runMemoryOut = runMemory;
    return RUN_STATUS_OK;
}

/* Allocates aligned payload storage, splitting a free block when practical. */
RunStatus run_memory_allocate(RunMemory *runMemory,
                              RUN_SIZE runSize,
                              RUN_SIZE runAlignment,
                              RUN_VOID **runBlockOut)
{
    RunMemoryBlock *runBlock;
    RUN_SIZE runNeed;

    if ((runMemory == RUN_NULL) || (runBlockOut == RUN_NULL) || (runSize == 0U))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    if (runAlignment == 0U)
    {
        runAlignment = RUN_MEMORY_ALIGNMENT;
    }
    if ((runAlignment > RUN_MEMORY_ALIGNMENT) ||
        ((runAlignment & (runAlignment - 1U)) != 0U))
    {
        return RUN_STATUS_UNSUPPORTED;
    }

    runNeed = run_memory_align_up(runSize, RUN_MEMORY_ALIGNMENT);
    if (runNeed > (RUN_SIZE_MAX - sizeof(RunMemoryBlock)))
    {
        runMemory->runFailureCount++;
        return RUN_STATUS_OVERFLOW;
    }
    runNeed += sizeof(RunMemoryBlock);
    if (runNeed < (sizeof(RunMemoryBlock) + RUN_MEMORY_BLOCK_MIN_PAYLOAD))
    {
        runNeed = sizeof(RunMemoryBlock) + RUN_MEMORY_BLOCK_MIN_PAYLOAD;
    }

    runBlock = run_memory_find(runMemory, runNeed);
    if (runBlock == RUN_NULL)
    {
        runMemory->runFailureCount++;
        *runBlockOut = RUN_NULL;
        return RUN_STATUS_NO_MEMORY;
    }
    run_memory_remove(runMemory, runBlock);

    /*
     * Split only if the tail can carry a header and two intrusive links.
     * Otherwise internal fragmentation is cheaper than an unusable block.
     */
    if ((runBlock->runSize - runNeed) >=
        (sizeof(RunMemoryBlock) + RUN_MEMORY_BLOCK_MIN_PAYLOAD))
    {
        RunMemoryBlock *runRemainder =
            (RunMemoryBlock *)(RUN_VOID *)((RUN_UINT_8 *)runBlock + runNeed);
        RunMemoryBlock *runNextPhysical =
            (RunMemoryBlock *)(RUN_VOID *)((RUN_UINT_8 *)runBlock + runBlock->runSize);

        runRemainder->runSize = runBlock->runSize - runNeed;
        /* Physical back-links make both adjacent merges constant-time candidates. */
        runRemainder->runPreviousPhysical = runBlock;
        runBlock->runSize = runNeed;
        if ((RUN_UINT_8 *)runNextPhysical <
            ((RUN_UINT_8 *)runMemory->runFirstBlock + runMemory->runCapacity))
        {
            runNextPhysical->runPreviousPhysical = runRemainder;
        }
        run_memory_insert(runMemory, runRemainder);
    }

    runBlock->runMagic = RUN_MEMORY_BLOCK_USED;
    runMemory->runUsed += runBlock->runSize;
    if (runMemory->runUsed > runMemory->runPeakUsed)
    {
        runMemory->runPeakUsed = runMemory->runUsed;
    }
    runMemory->runAllocationCount++;
    *runBlockOut = (RUN_VOID *)((RUN_UINT_8 *)runBlock + sizeof(RunMemoryBlock));
    return RUN_STATUS_OK;
}

/* Releases a payload and immediately coalesces adjacent free physical blocks. */
RunStatus run_memory_release(RunMemory *runMemory, RUN_VOID *runBlock)
{
    RunMemoryBlock *runCurrent;
    RunMemoryBlock *runPrevious;
    RunMemoryBlock *runNext;
    RUN_SIZE runArenaEnd;

    if ((runMemory == RUN_NULL) || (runBlock == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runCurrent = (RunMemoryBlock *)(RUN_VOID *)((RUN_UINT_8 *)runBlock - sizeof(RunMemoryBlock));
    if (runCurrent->runMagic != RUN_MEMORY_BLOCK_USED)
    {
        runMemory->runFailureCount++;
        return RUN_STATUS_INVALID_ARGUMENT;
    }

    runMemory->runUsed -= runCurrent->runSize;
    runMemory->runFreeCount++;
    runArenaEnd = (RUN_SIZE)runMemory->runFirstBlock + runMemory->runCapacity;

    /* Merge the immediate successor first, then fold into the predecessor. */
    runNext = (RunMemoryBlock *)(RUN_VOID *)((RUN_UINT_8 *)runCurrent + runCurrent->runSize);
    if (((RUN_SIZE)runNext < runArenaEnd) && (runNext->runMagic == RUN_MEMORY_BLOCK_FREE))
    {
        run_memory_remove(runMemory, runNext);
        runCurrent->runSize += runNext->runSize;
        runNext = (RunMemoryBlock *)(RUN_VOID *)((RUN_UINT_8 *)runCurrent + runCurrent->runSize);
    }
    if ((RUN_SIZE)runNext < runArenaEnd)
    {
        runNext->runPreviousPhysical = runCurrent;
    }

    runPrevious = runCurrent->runPreviousPhysical;
    if ((runPrevious != RUN_NULL) && (runPrevious->runMagic == RUN_MEMORY_BLOCK_FREE))
    {
        run_memory_remove(runMemory, runPrevious);
        runPrevious->runSize += runCurrent->runSize;
        runCurrent = runPrevious;
        runNext = (RunMemoryBlock *)(RUN_VOID *)((RUN_UINT_8 *)runCurrent + runCurrent->runSize);
        if ((RUN_SIZE)runNext < runArenaEnd)
        {
            runNext->runPreviousPhysical = runCurrent;
        }
    }

    run_memory_insert(runMemory, runCurrent);
    return RUN_STATUS_OK;
}

/* Copies current usage, peak, and largest-free-block statistics. */
RUN_VOID run_memory_get_stats(RunMemory *runMemory, RunMemoryStats *runStats)
{
    RunMemoryBlock *runBlock;
    RUN_SIZE runLargest = 0U;
    RUN_SIZE runArenaEnd;

    if ((runMemory == RUN_NULL) || (runStats == RUN_NULL))
    {
        return;
    }
    runBlock = runMemory->runFirstBlock;
    runArenaEnd = (RUN_SIZE)runBlock + runMemory->runCapacity;
    while (((RUN_SIZE)runBlock < runArenaEnd) && (runBlock->runSize != 0U))
    {
        if ((runBlock->runMagic == RUN_MEMORY_BLOCK_FREE) &&
            (runBlock->runSize > runLargest))
        {
            runLargest = runBlock->runSize;
        }
        runBlock = (RunMemoryBlock *)(RUN_VOID *)((RUN_UINT_8 *)runBlock + runBlock->runSize);
    }

    runStats->runCapacity = runMemory->runCapacity;
    runStats->runUsed = runMemory->runUsed;
    runStats->runPeakUsed = runMemory->runPeakUsed;
    runStats->runLargestFree = runLargest;
    runStats->runAllocationCount = runMemory->runAllocationCount;
    runStats->runFreeCount = runMemory->runFreeCount;
    runStats->runFailureCount = runMemory->runFailureCount;
}

/* Walks physical neighbors to verify allocator sizes and back-links. */
RunStatus run_memory_check(RunMemory *runMemory)
{
    RunMemoryBlock *runBlock;
    RunMemoryBlock *runPrevious = RUN_NULL;
    RUN_SIZE runArenaEnd;

    if ((runMemory == RUN_NULL) || (runMemory->runFirstBlock == RUN_NULL))
    {
        return RUN_STATUS_NOT_INITIALIZED;
    }
    runBlock = runMemory->runFirstBlock;
    runArenaEnd = (RUN_SIZE)runBlock + runMemory->runCapacity;
    while ((RUN_SIZE)runBlock < runArenaEnd)
    {
        if ((runBlock->runSize < (sizeof(RunMemoryBlock) + RUN_MEMORY_BLOCK_MIN_PAYLOAD)) ||
            (runBlock->runPreviousPhysical != runPrevious) ||
            ((runBlock->runMagic != RUN_MEMORY_BLOCK_FREE) &&
             (runBlock->runMagic != RUN_MEMORY_BLOCK_USED)))
        {
            return RUN_STATUS_ERROR;
        }
        runPrevious = runBlock;
        runBlock = (RunMemoryBlock *)(RUN_VOID *)((RUN_UINT_8 *)runBlock + runBlock->runSize);
    }
    return ((RUN_SIZE)runBlock == runArenaEnd) ? RUN_STATUS_OK : RUN_STATUS_ERROR;
}

/* Copies a non-overlapping byte range without a libc dependency. */
RUN_VOID *run_memory_copy(RUN_VOID *runDestination,
                          RUN_CONST RUN_VOID *runSource,
                          RUN_SIZE runSize)
{
    RUN_UINT_8 *runDestinationBytes = (RUN_UINT_8 *)runDestination;
    RUN_CONST RUN_UINT_8 *runSourceBytes = (RUN_CONST RUN_UINT_8 *)runSource;
    for (RUN_SIZE runIndex = 0U; runIndex < runSize; runIndex++)
    {
        runDestinationBytes[runIndex] = runSourceBytes[runIndex];
    }
    return runDestination;
}

/* Moves overlapping byte ranges in the safe direction. */
RUN_VOID *run_memory_move(RUN_VOID *runDestination,
                          RUN_CONST RUN_VOID *runSource,
                          RUN_SIZE runSize)
{
    RUN_UINT_8 *runDestinationBytes = (RUN_UINT_8 *)runDestination;
    RUN_CONST RUN_UINT_8 *runSourceBytes = (RUN_CONST RUN_UINT_8 *)runSource;
    /*
     * Copy toward increasing addresses when the destination starts before the
     * source, and backward when ranges may overlap in the other direction.
     */
    if (runDestinationBytes < runSourceBytes)
    {
        for (RUN_SIZE runIndex = 0U; runIndex < runSize; runIndex++)
        {
            runDestinationBytes[runIndex] = runSourceBytes[runIndex];
        }
    }
    else if (runDestinationBytes > runSourceBytes)
    {
        for (RUN_SIZE runIndex = runSize; runIndex > 0U; runIndex--)
        {
            runDestinationBytes[runIndex - 1U] = runSourceBytes[runIndex - 1U];
        }
    }
    return runDestination;
}

/* Fills a byte range with the low byte of the supplied value. */
RUN_VOID *run_memory_set(RUN_VOID *runDestination, RUN_INT_32 runValue, RUN_SIZE runSize)
{
    RUN_UINT_8 *runBytes = (RUN_UINT_8 *)runDestination;
    for (RUN_SIZE runIndex = 0U; runIndex < runSize; runIndex++)
    {
        runBytes[runIndex] = (RUN_UINT_8)runValue;
    }
    return runDestination;
}

/* Lexicographically compares two byte ranges. */
RUN_INT_32 run_memory_compare(RUN_CONST RUN_VOID *runLeft,
                              RUN_CONST RUN_VOID *runRight,
                              RUN_SIZE runSize)
{
    RUN_CONST RUN_UINT_8 *runLeftBytes = (RUN_CONST RUN_UINT_8 *)runLeft;
    RUN_CONST RUN_UINT_8 *runRightBytes = (RUN_CONST RUN_UINT_8 *)runRight;
    for (RUN_SIZE runIndex = 0U; runIndex < runSize; runIndex++)
    {
        if (runLeftBytes[runIndex] != runRightBytes[runIndex])
        {
            return (RUN_INT_32)runLeftBytes[runIndex] - (RUN_INT_32)runRightBytes[runIndex];
        }
    }
    return 0;
}

