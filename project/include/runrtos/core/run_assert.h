/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - assertion and panic controls
 *
 * Layer: core
 * Purpose: Provide one fatal-error path for internal invariants and user hooks.
 * Dependencies: generated configuration and core types.
 * Constraint: run_panic is non-returning; production images may compile out
 *             assertions, but explicit RunStatus checks must remain.
 */
#ifndef RUN_ASSERT_H_
#define RUN_ASSERT_H_

#include "runrtos/core/run_config.h"
#include "runrtos/core/run_types.h"

typedef RUN_VOID (*RunAssertHandler)(RUN_CONST RUN_CHAR *runExpression,
                                     RUN_CONST RUN_CHAR *runFile,
                                     RUN_UINT_32 runLine,
                                     RunStatus runStatus);

/* The hook runs just before panic and must return only to allow the panic path. */
RUN_VOID run_assert_set_handler(RunAssertHandler runHandler);

/*
 * The weak default never returns. A board may override it to dump state and
 * reset; returning from an override violates the API contract and is undefined.
 */
RUN_ATTR_NORETURN RUN_VOID run_panic(RUN_CONST RUN_CHAR *runReason,
                                     RUN_CONST RUN_CHAR *runFile,
                                     RUN_UINT_32 runLine,
                                     RunStatus runStatus);
RUN_BOOL run_assert_fail(RUN_CONST RUN_CHAR *runExpression,
                         RUN_CONST RUN_CHAR *runFile,
                         RUN_UINT_32 runLine,
                         RunStatus runStatus);

#if RUN_CONFIG_ASSERT_ENABLED
#define RUN_ASSERT(_runExpression, _runStatus) \
    ((_runExpression) ? RUN_TRUE : \
        run_assert_fail(#_runExpression, __FILE__, (RUN_UINT_32)__LINE__, (_runStatus)))
#define RUN_INVARIANT(_runExpression) RUN_ASSERT((_runExpression), RUN_STATUS_ERROR)
#else
#define RUN_ASSERT(_runExpression, _runStatus) ((RUN_VOID)0)
#define RUN_INVARIANT(_runExpression) ((RUN_VOID)0)
#endif

#endif

