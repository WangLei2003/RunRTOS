# SPDX-License-Identifier: Apache-2.0
# RunRTOS - STM32F103RBT6 board-support target
#
# Layer: bsp/f103rb
# Purpose: Build board clocks, pin setup, arena, and device composition.
# Dependencies: STM32F1 driver target and Cortex-M3 architecture port.
# Constraint: Board pin and clock policy must not leak into generic drivers.

add_library(runrtos_bsp_f103rb STATIC
    board.c
    external_storage.c
)
target_link_libraries(runrtos_bsp_f103rb PUBLIC
    runrtos_arch_cortex_m3
    runrtos_drivers_stm32f1
)
runrtos_apply_c_warnings(runrtos_bsp_f103rb)

