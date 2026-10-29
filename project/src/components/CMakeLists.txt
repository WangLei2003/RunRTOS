# SPDX-License-Identifier: Apache-2.0
# RunRTOS - optional component composition
#
# Layer: components
# Purpose: Add independently selectable diagnostic and utility components.
# Dependencies: BSP, kernel, driver, and runtime public targets.
# Constraint: Each component remains a separate static library for pruning.

add_subdirectory(selftest)
add_subdirectory(stress)
if(RUNRTOS_SHELL)
    add_subdirectory(shell)
endif()
