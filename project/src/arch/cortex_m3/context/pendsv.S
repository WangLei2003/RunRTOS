/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - Cortex-M3 PendSV context switch
 *
 * Layer: arch/cortex_m3/context
 * Purpose: Save/restore callee-saved registers and switch the process stack.
 * Dependencies: RunKernelCurrent offset-zero stack ABI and run_kernel_switch_prepare.
 * Constraint: Hardware already saved r0-r3, r12, LR, PC, and xPSR on entry.
 */
    .syntax unified
    .thumb
    .text
    .global PendSV_Handler
    .type PendSV_Handler, %function
PendSV_Handler:
    @ Hardware has already stacked the caller-saved exception frame on PSP.
    mrs r0, psp
    @ Decrement first so the saved pointer denotes the complete software frame.
    stmdb r0!, {r4-r11}

    @ Offset zero in RunThread is the saved PSP; this is a kernel/port ABI.
    ldr r1, =RunKernelCurrent
    ldr r1, [r1]
    str r0, [r1, #0]

    bl run_kernel_switch_prepare

    @ The callback may have selected a different thread; reload both symbols.
    ldr r1, =RunKernelCurrent
    ldr r1, [r1]
    ldr r0, [r1, #0]
    ldmia r0!, {r4-r11}
    msr psp, r0
    @ EXC_RETURN selects thread mode with PSP and no FPU extended frame.
    mov lr, #0xFFFFFFFD
    bx lr
    .size PendSV_Handler, .-PendSV_Handler

    .end

