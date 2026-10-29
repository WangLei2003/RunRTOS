/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - Cortex-M3 reset initialization
 *
 * Layer: arch/cortex_m3/startup
 * Purpose: Copy initialized data, clear BSS, then enter the application.
 * Dependencies: linker symbols from stm32f103rb.ld and run_application_start.
 * Constraint: Runs on MSP before clocks, allocator, or scheduler initialization.
 */
    .syntax unified
    .thumb
    .text
    .global Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    @ Copy initialized data from its Flash load address into SRAM.
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
1:
    cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:
    @ BSS must be zero because C objects rely on zero-initialized state.
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
3:
    cmp r0, r1
    bcs 4f
    str r2, [r0], #4
    b 3b
4:
    @ Only run_application_start returns here; it is declared non-returning.
    bl run_application_start
    @ Trap reset-handler entry if application initialization violates that contract.
5:
    b 5b
    .size Reset_Handler, .-Reset_Handler

    .end

