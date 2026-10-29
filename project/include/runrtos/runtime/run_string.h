/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - owned and borrowed strings
 *
 * Layer: runtime
 * Purpose: Provide non-owning RunStringView and allocator-backed RunString.
 * Dependencies: runtime memory allocator.
 * Constraint: Views never outlive their source; owned strings must be deinitialized
 *             with the same allocator that created them.
 */
#ifndef RUN_STRING_H_
#define RUN_STRING_H_

#include "runrtos/runtime/run_memory.h"

typedef RUN_STRUCT RunStringView
{
    RUN_CONST RUN_CHAR *runData;
    RUN_SIZE runLength;
} RunStringView;

typedef RUN_STRUCT RunString
{
    RunMemory *runMemory;
    RUN_CHAR *runData;
    RUN_SIZE runLength;
    RUN_SIZE runCapacity;
} RunString;

#define RUN_STRING_VIEW(_runLiteral) \
    { (_runLiteral), sizeof(_runLiteral) - (RUN_SIZE)1U }
#define RUN_STRING_VIEW_EMPTY { RUN_NULL, (RUN_SIZE)0U }

/* A null C string has length zero; compare and equality use explicit lengths. */
RUN_SIZE run_string_length(RUN_CONST RUN_CHAR *runText);
RUN_INT_32 run_string_compare_view(RunStringView runLeft, RunStringView runRight);
RUN_BOOL run_string_equal_view(RunStringView runLeft, RunStringView runRight);

/*
 * Initializes an uninitialized RunString with allocator-owned, NUL-terminated
 * storage. Assign replaces the descriptor and must not be called on live storage
 * without run_string_deinit first.
 */
RunStatus run_string_from_cstr(RunMemory *runMemory,
                               RUN_CONST RUN_CHAR *runText,
                               RunString *runString);
RunStatus run_string_assign_view(RunMemory *runMemory,
                                 RunStringView runView,
                                 RunString *runString);

/*
 * Appending grows into temporary storage before releasing the old buffer, so a
 * failed append leaves the original value unchanged. The returned view is
 * borrowed and becomes invalid after mutation, deinitialization, or arena reset.
 */
RunStatus run_string_append_view(RunString *runString, RunStringView runView);
RunStringView run_string_as_view(RUN_CONST RunString *runString);
RUN_VOID run_string_deinit(RunString *runString);

#endif

