# SPDX-License-Identifier: Apache-2.0
# RunRTOS - STM32F1 register-level driver target
#
# Layer: drivers/soc/stm32f1
# Purpose: Build register-level GPIO, UART, SPI, and I2C backends.
# Dependencies: Cortex-M3 arch cycle counter and generic driver interfaces.
# Constraint: Only this target may encode STM32F1 peripheral register addresses.

add_library(runrtos_drivers_stm32f1 STATIC
    gpio/gpio.c
    i2c/i2c.c
    spi/spi.c
    uart/uart.c
)
target_link_libraries(runrtos_drivers_stm32f1 PUBLIC
    runrtos_arch_cortex_m3
    runrtos_drivers
)
runrtos_apply_c_warnings(runrtos_drivers_stm32f1)

