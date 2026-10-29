/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - stable status names for diagnostics
 *
 * Layer: core
 * Purpose: Map RunStatus values to constant, allocation-free text.
 * Dependencies: public core types.
 * Constraint: Returned strings have static lifetime and must not be freed.
 */
#include "runrtos/core/run_types.h"

RUN_CONST RUN_CHAR *run_status_name(RunStatus runStatus)
{
    switch (runStatus)
    {
    case RUN_STATUS_OK: return "ok";
    case RUN_STATUS_ERROR: return "error";
    case RUN_STATUS_INVALID_ARGUMENT: return "invalid argument";
    case RUN_STATUS_INVALID_STATE: return "invalid state";
    case RUN_STATUS_NOT_INITIALIZED: return "not initialized";
    case RUN_STATUS_NO_MEMORY: return "no memory";
    case RUN_STATUS_TIMEOUT: return "timeout";
    case RUN_STATUS_FULL: return "full";
    case RUN_STATUS_EMPTY: return "empty";
    case RUN_STATUS_BUSY: return "busy";
    case RUN_STATUS_NOT_OWNER: return "not owner";
    case RUN_STATUS_OVERFLOW: return "overflow";
    case RUN_STATUS_CANCELLED: return "cancelled";
    case RUN_STATUS_UNSUPPORTED: return "unsupported";
    case RUN_STATUS_ALREADY_EXISTS: return "already exists";
    case RUN_STATUS_NOT_FOUND: return "not found";
    case RUN_STATUS_CONTEXT: return "invalid context";
    default: return "unknown";
    }
}

