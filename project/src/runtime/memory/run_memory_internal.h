/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - private memory block layout
 *
 * Layer: runtime (private)
 * Purpose: Define physical block headers and intrusive free-list links.
 * Dependencies: public run_memory.h.
 * Constraint: Every block size includes its header and stays 8-byte aligned;
 *             Magic values are diagnostics, not ownership synchronization.
 */
#ifndef RUN_MEMORY_INTERNAL_H_
#define RUN_MEMORY_INTERNAL_H_

#include "runrtos/runtime/run_memory.h"

#define RUN_MEMORY_BLOCK_FREE 0x52554E46U
#define RUN_MEMORY_BLOCK_USED 0x52554E41U
#define RUN_MEMORY_BLOCK_MIN_PAYLOAD (sizeof(RUN_VOID *) * 2U)

typedef RUN_STRUCT RunMemoryFreeLinks
{
    RUN_STRUCT RunMemoryBlock *runNext;
    RUN_STRUCT RunMemoryBlock *runPrevious;
} RunMemoryFreeLinks;

typedef RUN_STRUCT RunMemoryBlock
{
    RUN_SIZE runSize;
    RUN_STRUCT RunMemoryBlock *runPreviousPhysical;
    RUN_UINT_32 runMagic;
    RUN_UINT_32 runReserved;
} RunMemoryBlock;

RUN_STRUCT RunMemory
{
    RunMemoryBlock *runFirstBlock;
    RunMemoryBlock *runFreeHeads[32];
    RUN_UINT_32 runFreeBitmap;
    RUN_SIZE runCapacity;
    RUN_SIZE runUsed;
    RUN_SIZE runPeakUsed;
    RUN_UINT_32 runAllocationCount;
    RUN_UINT_32 runFreeCount;
    RUN_UINT_32 runFailureCount;
};

#endif

