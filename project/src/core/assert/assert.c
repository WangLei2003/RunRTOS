/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - assertion dispatch and default panic loop
 *
 * Layer: core
 * Purpose: Invoke the optional diagnostic hook before entering the fatal path.
 * Dependencies: public assert contract.
 * Constraint: The default panic implementation cannot recover or return.
 */
#include "runrtos/core/run_assert.h"

RUN_STATIC RunAssertHandler RunAssertHandlerInstance;

/* Installs or clears the optional user assertion hook. */
RUN_VOID run_assert_set_handler(RunAssertHandler runHandler)
{
    RunAssertHandlerInstance = runHandler;
}

/* Reports a failed assertion, then enters the non-returning panic path. */
RUN_BOOL run_assert_fail(RUN_CONST RUN_CHAR *runExpression,
                         RUN_CONST RUN_CHAR *runFile,
                         RUN_UINT_32 runLine,
                         RunStatus runStatus)
{
    if (RunAssertHandlerInstance != RUN_NULL)
    {
        RunAssertHandlerInstance(runExpression, runFile, runLine, runStatus);
    }
    run_panic(runExpression, runFile, runLine, runStatus);
    return RUN_FALSE;
}

/* Provides the weak fail-safe panic target until a board overrides it. */
RUN_ATTR_WEAK RUN_ATTR_NORETURN RUN_VOID run_panic(RUN_CONST RUN_CHAR *runReason,
                                                   RUN_CONST RUN_CHAR *runFile,
                                                   RUN_UINT_32 runLine,
                                                   RunStatus runStatus)
{
    (RUN_VOID)runReason;
    (RUN_VOID)runFile;
    (RUN_VOID)runLine;
    (RUN_VOID)runStatus;
    for (;;)
    {
        __asm__ __volatile__("nop");
    }
}

