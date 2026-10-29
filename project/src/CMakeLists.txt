# SPDX-License-Identifier: Apache-2.0
# RunRTOS - source-layer composition
#
# Layer: build/src
# Purpose: Add targets in dependency order and omit hardware targets on host builds.
# Dependencies: Child core, runtime, kernel, arch, driver, BSP, and application dirs.
# Constraint: Every enabled child layer may depend only on targets below it.

add_subdirectory(core)
add_subdirectory(runtime)
add_subdirectory(kernel)

if(NOT RUNRTOS_HOST_TESTS)
    add_subdirectory(arch/cortex_m3)
endif()

add_subdirectory(drivers)

if(NOT RUNRTOS_HOST_TESTS)
    add_subdirectory(bsp/f103rb)
    add_subdirectory(applications/demo)
endif()

