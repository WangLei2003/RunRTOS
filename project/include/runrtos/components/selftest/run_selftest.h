/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - board and kernel self-test service
 *
 * Layer: components/selftest
 * Purpose: Run nonblocking-context diagnostic checks and emit PASS/FAIL/SKIP lines.
 * Dependencies: public board, kernel, driver, and runtime interfaces.
 * Constraint: Execute from a normal thread. External-memory checks are reversible
 *             when uninterrupted, but a power loss may interrupt restoration.
 */
#ifndef RUN_SELFTEST_H_
#define RUN_SELFTEST_H_

#include "runrtos/bsp/run_board.h"

typedef RUN_VOID (*RunSelfTestWrite)(RUN_CONST RUN_CHAR *runText,
                                     RUN_SIZE runLength,
                                     RUN_VOID *runContext);

/*
 * Runs every check in sequence. The writer is mandatory and may be invoked
 * repeatedly. Errors do not stop later checks; the return value is ERROR if any
 * check failed and OK when all checks passed or skipped.
 */
RunStatus run_selftest_execute(RunBoardDevices *runDevices,
                               RunSelfTestWrite runWrite,
                               RUN_VOID *runWriteContext);

#endif
