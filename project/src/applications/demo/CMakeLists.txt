# SPDX-License-Identifier: Apache-2.0
# RunRTOS - F103 demo application and final ELF image
#
# Layer: applications/demo
# Purpose: Build the demo threads and link the final freestanding firmware image.
# Dependencies: Public RunRTOS APIs and the F103RB board target.
# Constraint: Only the final executable may name the linker script and libgcc.
# The final image is linked freestanding against the generated linker script and
# only the explicitly permitted GCC arithmetic helper runtime.

add_library(runrtos_demo STATIC
    demo.c
)
target_link_libraries(runrtos_demo PUBLIC
    runrtos_bsp_f103rb
    runrtos_kernel
)
runrtos_apply_c_warnings(runrtos_demo)

add_executable(runrtos_firmware
    firmware.c
)
target_link_options(runrtos_firmware PRIVATE
    -T${RUNRTOS_LINKER_SCRIPT}
    -nostdlib
    -nostartfiles
    -Wl,--gc-sections
    -Wl,--no-warn-rwx-segments
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/runrtos_firmware.map
)
target_link_libraries(runrtos_firmware PRIVATE
    -Wl,--start-group
    runrtos_demo
    runrtos_bsp_f103rb
    runrtos_drivers_stm32f1
    runrtos_drivers
    runrtos_arch_cortex_m3
    runrtos_kernel
    runrtos_runtime
    runrtos_core
    -lgcc
    -Wl,--end-group
)
runrtos_apply_c_warnings(runrtos_firmware)

add_custom_command(TARGET runrtos_firmware POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:runrtos_firmware>
            ${CMAKE_CURRENT_BINARY_DIR}/runrtos_firmware.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:runrtos_firmware>
            ${CMAKE_CURRENT_BINARY_DIR}/runrtos_firmware.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:runrtos_firmware>
    VERBATIM
)

