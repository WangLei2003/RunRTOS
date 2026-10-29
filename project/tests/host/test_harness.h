/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - host test assertions and suite declarations
 *
 * Layer: tests/host
 * Purpose: Provide a tiny dependency-free assertion vocabulary shared by suites.
 * Dependencies: RunRTOS types and host test runner output routines.
 * Constraint: A failed assertion is recorded and execution continues; fatal
 *             scheduler faults are still reported by the process exit code.
 */
#ifndef RUN_TEST_HARNESS_H_
#define RUN_TEST_HARNESS_H_

#include "runrtos/core/run_assert.h"
#include "runrtos/core/run_types.h"
#include "runrtos/kernel/run_sync.h"

typedef RUN_STRUCT RunTestContext
{
    RUN_CONST RUN_CHAR *runSuite;
    RUN_UINT_32 runChecks;
    RUN_UINT_32 runFailures;
} RunTestContext;

RUN_VOID run_test_check(RunTestContext *runContext,
                        RUN_BOOL runCondition,
                        RUN_CONST RUN_CHAR *runExpression,
                        RUN_UINT_32 runLine);
RUN_VOID run_test_suite_begin(RunTestContext *runContext,
                              RUN_CONST RUN_CHAR *runSuite);
RUN_VOID run_test_report_failure(RunTestContext *runContext,
                                 RUN_CONST RUN_CHAR *runExpression,
                                 RUN_UINT_32 runLine);
RUN_VOID run_test_output(RUN_CONST RUN_CHAR *runText);

#define RUN_TEST_CHECK(_runContext, _runCondition) \
    run_test_check((_runContext), (_runCondition) ? RUN_TRUE : RUN_FALSE, \
                   #_runCondition, (RUN_UINT_32)__LINE__)
#define RUN_TEST_STATUS(_runContext, _runActual, _runExpected) \
    RUN_TEST_CHECK((_runContext), ((_runActual) == (_runExpected)))

RUN_VOID run_test_runtime_suite(RunTestContext *runContext);
RUN_VOID run_test_kernel_suite(RunTestContext *runContext,
                               RunSemaphore *runKickSemaphore);
RUN_VOID run_test_sync_suite(RunTestContext *runContext,
                             RunSemaphore *runKickSemaphore);
RUN_VOID run_test_driver_suite(RunTestContext *runContext);

#endif
