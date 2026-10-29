/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - F103 demo application entry
 *
 * Layer: applications
 * Purpose: Publish the firmware entry called by the architecture reset code.
 * Dependencies: core types only.
 * Constraint: This function owns board initialization and never returns.
 */
#ifndef RUN_DEMO_H_
#define RUN_DEMO_H_

#include "runrtos/core/run_types.h"

RUN_ATTR_NORETURN RUN_VOID run_application_start(RUN_VOID);

#endif

