/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - freestanding formatting and integer parsing
 *
 * Layer: runtime
 * Purpose: Support bounded diagnostics without stdio or libc.
 * Dependencies: string and memory runtime services.
 * Constraint: Buffer formatting always NUL-terminates when capacity is nonzero
 *             and reports truncation through RUN_STATUS_OVERFLOW.
 */
#ifndef RUN_FORMAT_H_
#define RUN_FORMAT_H_

#include "runrtos/runtime/run_string.h"

typedef RUN_VOID (*RunFormatWrite)(RUN_CONST RUN_CHAR *runData,
                                   RUN_SIZE runLength,
                                   RUN_VOID *runContext);

/*
 * Supports %%, %c, %s, %d/%i, %u, %x/%X, and %p with optional zero/space width
 * plus l, ll, and z integer-size modifiers. Unknown conversions are emitted
 * literally. The write callback is mandatory and may be invoked many times.
 */
RunStatus run_format(RunFormatWrite runWrite,
                     RUN_VOID *runContext,
                     RUN_CONST RUN_CHAR *runFormat,
                     RUN_VA_LIST runArguments);

/*
 * Bounded formatting always writes a terminating NUL when the buffer is valid.
 * Truncation returns RUN_STATUS_OVERFLOW; runLengthOut receives bytes retained,
 * excluding the terminator. Parse helpers accept decimal digits only and reject
 * trailing characters or u32 overflow.
 */
RunStatus run_format_buffer(RUN_CHAR *runBuffer,
                            RUN_SIZE runBufferSize,
                            RUN_SIZE *runLengthOut,
                            RUN_CONST RUN_CHAR *runFormat,
                            ...);
RUN_UINT_64 run_format_parse_u64(RUN_CONST RUN_CHAR *runText, RUN_BOOL *runValidOut);
RUN_BOOL run_format_parse_u32(RUN_CONST RUN_CHAR *runText, RUN_UINT_32 *runValueOut);

#endif

