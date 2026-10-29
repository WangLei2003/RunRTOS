/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - firmware executable anchor
 *
 * Layer: applications/demo
 * Purpose: Give CMake an application-owned translation unit for the final ELF.
 * Dependencies: core types only.
 * Constraint: Reset and application entry are provided by startup.S and demo.c.
 */
#include "runrtos/core/run_types.h"

/* Keeps the final executable target anchored to application-owned code. */
RUN_VOID run_firmware_anchor(RUN_VOID)
{
}

