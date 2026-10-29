# SPDX-License-Identifier: Apache-2.0
# RunRTOS - background stress component
#
# Layer: components/stress
# Purpose: Build the reusable allocator and IPC stress worker.
# Dependencies: Public kernel, synchronization, and runtime targets.
# Constraint: No board registers or Shell formatting are referenced.

add_library(runrtos_stress STATIC
    stress.c
)
target_link_libraries(runrtos_stress PUBLIC
    runrtos_kernel
    runrtos_runtime
)
runrtos_apply_c_warnings(runrtos_stress)
