# SPDX-License-Identifier: Apache-2.0
# RunRTOS - UART diagnostic shell component
#
# Layer: components/shell
# Purpose: Build the independent Shell thread and standard 0.1 commands.
# Dependencies: BSP, selftest, stress, kernel, driver, and runtime targets.
# Constraint: The Shell owns no hardware registers and can be omitted by CMake.

add_library(runrtos_shell STATIC
    shell.c
)
target_link_libraries(runrtos_shell PUBLIC
    runrtos_bsp_f103rb
    runrtos_selftest
    runrtos_stress
    runrtos_runtime
)
runrtos_apply_c_warnings(runrtos_shell)
