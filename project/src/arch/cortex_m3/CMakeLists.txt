# SPDX-License-Identifier: Apache-2.0
# RunRTOS - Cortex-M3 and STM32F103RB architecture target
#
# Layer: arch/cortex_m3
# Purpose: Build startup, PendSV, interrupt vectors, and generated linker script.
# Dependencies: runrtos_core, runrtos_kernel, and GCC ARM assembler.
# Constraint: Port assembly is the only code coupled to the thread stack ABI.
# Also generates the linker script containing the configured main-stack size.

set(RUNRTOS_LINKER_SCRIPT
    ${CMAKE_CURRENT_BINARY_DIR}/stm32f103rb.ld
    CACHE INTERNAL "RunRTOS linker script"
)
configure_file(
    linker/stm32f103rb.ld.in
    ${RUNRTOS_LINKER_SCRIPT}
    @ONLY
)

add_library(runrtos_arch_cortex_m3 STATIC
    context/pendsv.S
    port/port.c
    startup/startup.S
)
target_include_directories(runrtos_arch_cortex_m3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../kernel/internal
)
target_link_libraries(runrtos_arch_cortex_m3 PUBLIC
    runrtos_core
    runrtos_kernel
)
runrtos_apply_c_warnings(runrtos_arch_cortex_m3)

