# SPDX-License-Identifier: Apache-2.0
# RunRTOS - generic device and capability-driver target
#
# Layer: drivers
# Purpose: Build generic device lifecycle and capability-forwarding interfaces.
# Dependencies: runrtos_core and runrtos_kernel allocation/context services.
# Constraint: Generic drivers dispatch through operation tables without register
#             knowledge; SOC backends are separate child targets.

add_library(runrtos_drivers STATIC
    core/device.c
    gpio/gpio.c
    i2c/i2c.c
    spi/spi.c
    uart/uart.c
)
target_link_libraries(runrtos_drivers PUBLIC
    runrtos_core
    runrtos_kernel
)
runrtos_apply_c_warnings(runrtos_drivers)

if(NOT RUNRTOS_HOST_TESTS)
    add_subdirectory(soc/stm32f1)
endif()

