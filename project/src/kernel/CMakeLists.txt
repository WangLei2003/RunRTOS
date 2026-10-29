# SPDX-License-Identifier: Apache-2.0
# RunRTOS - kernel target (scheduler, threads, IPC, timers)
#
# Layer: kernel
# Purpose: Build scheduler, thread lifecycle, IPC, and software timers.
# Dependencies: runrtos_core, runrtos_runtime, and private kernel ABI headers.
# Constraint: Architecture calls enter through run_arch.h, never register APIs.

add_library(runrtos_kernel STATIC
    scheduler/kernel.c
    synchronization/sync.c
)
target_include_directories(runrtos_kernel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/internal
)
target_link_libraries(runrtos_kernel PUBLIC
    runrtos_core
    runrtos_runtime
)
runrtos_apply_c_warnings(runrtos_kernel)

