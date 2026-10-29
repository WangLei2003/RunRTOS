/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - primitive, time, and status types
 *
 * Layer: core
 * Purpose: Define fixed-width primitives and the common API status contract.
 * Dependencies: run_compiler.h.
 * Constraint: Public and kernel code must not introduce native base types.
 */
#ifndef RUN_TYPES_H_
#define RUN_TYPES_H_

#include "runrtos/core/run_compiler.h"

RUN_TYPEDEF RUN_UINT_64 RunTimeMs;
RUN_TYPEDEF RUN_UINT_32 RunTick;

#define RUN_TIME_NO_WAIT ((RunTimeMs)0U)
#define RUN_TIME_FOREVER ((RunTimeMs)(~(RUN_UINT_64)0U))

/*
 * One status vocabulary is shared by every layer. Context-specific outcomes such
 * as EMPTY, FULL, BUSY, and TIMEOUT are intentionally distinct so callers can
 * handle expected contention without parsing diagnostic strings.
 */
RUN_TYPEDEF RUN_ENUM RunStatus
{
    RUN_STATUS_OK = 0,
    RUN_STATUS_ERROR,
    RUN_STATUS_INVALID_ARGUMENT,
    RUN_STATUS_INVALID_STATE,
    RUN_STATUS_NOT_INITIALIZED,
    RUN_STATUS_NO_MEMORY,
    RUN_STATUS_TIMEOUT,
    RUN_STATUS_FULL,
    RUN_STATUS_EMPTY,
    RUN_STATUS_BUSY,
    RUN_STATUS_NOT_OWNER,
    RUN_STATUS_OVERFLOW,
    RUN_STATUS_CANCELLED,
    RUN_STATUS_UNSUPPORTED,
    RUN_STATUS_ALREADY_EXISTS,
    RUN_STATUS_NOT_FOUND,
    RUN_STATUS_CONTEXT
} RunStatus;

/* Returns a constant diagnostic string; unknown values map to "unknown". */
RUN_CONST RUN_CHAR *run_status_name(RunStatus runStatus);

#endif

