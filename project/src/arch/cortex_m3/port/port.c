/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - Cortex-M3 port without CMSIS
 *
 * Layer: arch/cortex_m3
 * Purpose: Implement critical sections, PendSV handoff, vectors, and fault capture.
 * Dependencies: raw ARM Cortex-M system registers and private kernel ABI.
 * Constraint: PendSV and SysTick use the lowest interrupt priority; thread stacks
 *             are selected through PSP while exception entry uses MSP.
 */
#include "../../../kernel/internal/run_kernel_internal.h"
#include "runrtos/arch/run_arch.h"
#include "runrtos/core/run_assert.h"

#define RUN_SCB_ICSR (*(RUN_VOLATILE RUN_UINT_32 *)0xE000ED04U)
#define RUN_SCB_SHPR2 (*(RUN_VOLATILE RUN_UINT_32 *)0xE000ED1CU)
#define RUN_SCB_SHPR3 (*(RUN_VOLATILE RUN_UINT_32 *)0xE000ED20U)
#define RUN_SCB_AIRCR (*(RUN_VOLATILE RUN_UINT_32 *)0xE000ED0CU)
#define RUN_SCB_CFSR (*(RUN_VOLATILE RUN_UINT_32 *)0xE000ED28U)
#define RUN_SCB_CCR (*(RUN_VOLATILE RUN_UINT_32 *)0xE000ED14U)
#define RUN_DWT_CTRL (*(RUN_VOLATILE RUN_UINT_32 *)0xE0001000U)
#define RUN_DWT_CYCCNT (*(RUN_VOLATILE RUN_UINT_32 *)0xE0001004U)
#define RUN_DEMCR (*(RUN_VOLATILE RUN_UINT_32 *)0xE000EDFCU)
#define RUN_SCB_ICSR_PENDSVSET (1UL << 28U)
#define RUN_SCB_ICSR_PENDSVCLR (1UL << 27U)
#define RUN_SCB_AIRCR_SYSRESETREQ (1UL << 2U)
#define RUN_SCB_AIRCR_VECTKEY (0x05FAUL << 16U)
#define RUN_DEMCR_TRCENA (1UL << 24U)
#define RUN_DWT_CTRL_CYCCNTENA (1UL << 0U)
#define RUN_CORTEX_M_INITIAL_FRAME_WORDS 16U
#define RUN_CORTEX_M_XPSR_THUMB (1UL << 24U)

RUN_EXTERN RUN_UINT_32 _estack;

typedef RUN_STRUCT RunFaultFrame
{
    /* Cortex-M3 basic exception frame, ordered by increasing stack address. */
    RUN_UINT_32 runR0;
    RUN_UINT_32 runR1;
    RUN_UINT_32 runR2;
    RUN_UINT_32 runR3;
    RUN_UINT_32 runR12;
    RUN_UINT_32 runLr;
    RUN_UINT_32 runPc;
    RUN_UINT_32 runXpsr;
} RunFaultFrame;

RUN_VOLATILE RunFaultFrame RunFaultFrameLast;
RUN_VOLATILE RUN_UINT_32 RunFaultStatusLast;

RUN_VOID Reset_Handler(RUN_VOID);
RUN_VOID NMI_Handler(RUN_VOID);
RUN_VOID HardFault_Handler(RUN_VOID);
RUN_VOID MemManage_Handler(RUN_VOID);
RUN_VOID BusFault_Handler(RUN_VOID);
RUN_VOID UsageFault_Handler(RUN_VOID);
RUN_VOID SVC_Handler(RUN_VOID);
RUN_VOID DebugMon_Handler(RUN_VOID);
RUN_VOID PendSV_Handler(RUN_VOID);
RUN_VOID SysTick_Handler(RUN_VOID);
RUN_VOID Default_Handler(RUN_VOID);

RUN_ATTR_NORETURN RUN_VOID Default_Handler(RUN_VOID)
{
    run_panic("unhandled interrupt", __FILE__, (RUN_UINT_32)__LINE__, RUN_STATUS_ERROR);
}

RUN_ATTR_NORETURN RUN_VOID NMI_Handler(RUN_VOID)
{
    run_panic("NMI", __FILE__, (RUN_UINT_32)__LINE__, RUN_STATUS_ERROR);
}

RUN_ATTR_NORETURN RUN_VOID MemManage_Handler(RUN_VOID)
{
    run_panic("MemManage fault", __FILE__, (RUN_UINT_32)__LINE__, RUN_STATUS_ERROR);
}

RUN_ATTR_NORETURN RUN_VOID BusFault_Handler(RUN_VOID)
{
    run_panic("Bus fault", __FILE__, (RUN_UINT_32)__LINE__, RUN_STATUS_ERROR);
}

RUN_ATTR_NORETURN RUN_VOID UsageFault_Handler(RUN_VOID)
{
    run_panic("Usage fault", __FILE__, (RUN_UINT_32)__LINE__, RUN_STATUS_ERROR);
}

RUN_VOID SVC_Handler(RUN_VOID)
{
}

RUN_VOID DebugMon_Handler(RUN_VOID)
{
}

RUN_ATTR_NAKED RUN_VOID HardFault_Handler(RUN_VOID)
{
    /* EXC_RETURN bit 2 selects the interrupted thread's PSP instead of MSP. */
    __asm__ __volatile__(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b run_hard_fault_c\n");
}

RUN_ATTR_NORETURN RUN_VOID run_hard_fault_c(RunFaultFrame *runFrame)
{
    RunFaultFrameLast.runR0 = runFrame->runR0;
    RunFaultFrameLast.runR1 = runFrame->runR1;
    RunFaultFrameLast.runR2 = runFrame->runR2;
    RunFaultFrameLast.runR3 = runFrame->runR3;
    RunFaultFrameLast.runR12 = runFrame->runR12;
    RunFaultFrameLast.runLr = runFrame->runLr;
    RunFaultFrameLast.runPc = runFrame->runPc;
    RunFaultFrameLast.runXpsr = runFrame->runXpsr;
    /* CFSR groups usage, bus, and memory-management fault status bits. */
    RunFaultStatusLast = RUN_SCB_CFSR;
    run_panic("HardFault", __FILE__, (RUN_UINT_32)__LINE__, RUN_STATUS_ERROR);
}

RUN_VOID SysTick_Handler(RUN_VOID)
{
    run_kernel_tick_from_isr();
}

RUN_ATTR_USED RUN_ATTR_SECTION(".isr_vector")
RUN_CONST RUN_UINTPTR_TYPE RunVectorTable[] = {
    /* Cortex-M core vectors first, followed by STM32F103 peripheral IRQs. */
    (RUN_UINTPTR_TYPE)&_estack,
    (RUN_UINTPTR_TYPE)Reset_Handler,
    (RUN_UINTPTR_TYPE)NMI_Handler,
    (RUN_UINTPTR_TYPE)HardFault_Handler,
    (RUN_UINTPTR_TYPE)MemManage_Handler,
    (RUN_UINTPTR_TYPE)BusFault_Handler,
    (RUN_UINTPTR_TYPE)UsageFault_Handler,
    0U, 0U, 0U, 0U,
    (RUN_UINTPTR_TYPE)SVC_Handler,
    (RUN_UINTPTR_TYPE)DebugMon_Handler,
    0U,
    (RUN_UINTPTR_TYPE)PendSV_Handler,
    (RUN_UINTPTR_TYPE)SysTick_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler,
    (RUN_UINTPTR_TYPE)Default_Handler
};

RUN_VOID run_arch_early_init(RUN_VOID)
{
    /* SHPR2/SHPR3 hold SVC, PendSV, and SysTick priorities in top bytes. */
    RUN_SCB_SHPR2 = (RUN_SCB_SHPR2 & 0x00FFFFFFU) | (0xFFUL << 24U);
    RUN_SCB_SHPR3 = (RUN_SCB_SHPR3 & 0x0000FFFFU) | (0xFFFFUL << 16U);
    /* Enable DWT cycle counting for bounded polling drivers. */
    RUN_DEMCR |= RUN_DEMCR_TRCENA;
    RUN_DWT_CYCCNT = 0U;
    RUN_DWT_CTRL |= RUN_DWT_CTRL_CYCCNTENA;
}

RunStatus run_arch_context_initialize(RUN_CONST RunArchThreadContextConfig *runConfig,
                                      RUN_SIZE *runContextOut)
{
    RUN_SIZE *runTop;
    RUN_SIZE *runFrame;
    if ((runConfig == RUN_NULL) || (runContextOut == RUN_NULL) ||
        (runConfig->runStackBase == RUN_NULL) || (runConfig->runEntry == RUN_NULL) ||
        (runConfig->runReturn == RUN_NULL) ||
        (runConfig->runStackSize <
         sizeof(RUN_SIZE) * RUN_CORTEX_M_INITIAL_FRAME_WORDS))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runTop = (RUN_SIZE *)(RUN_VOID *)((RUN_UINT_8 *)runConfig->runStackBase +
                                      runConfig->runStackSize);
    runTop = (RUN_SIZE *)(RUN_VOID *)((RUN_SIZE)runTop & ~((RUN_SIZE)7U));
    /*
     * Reserve r4-r11 (8 words), then the hardware r0-r3/r12/LR/PC/xPSR frame
     * (8 words). This layout is private to the Cortex-M port.
     */
    runFrame = runTop - RUN_CORTEX_M_INITIAL_FRAME_WORDS;
    run_memory_set(runFrame, 0, RUN_CORTEX_M_INITIAL_FRAME_WORDS * sizeof(RUN_SIZE));
    runFrame[8] = (RUN_SIZE)(RUN_UINTPTR_TYPE)runConfig->runEntryArgument;
    runFrame[13] = (RUN_SIZE)(RUN_UINTPTR_TYPE)runConfig->runReturn;
    runFrame[14] = (RUN_SIZE)(RUN_UINTPTR_TYPE)runConfig->runEntry;
    runFrame[15] = RUN_CORTEX_M_XPSR_THUMB;
    /*
     * PendSV pre-decrements PSP before saving r4-r11 on the first switch.
     * Point it at the hardware frame so the save lands in the reserved
     * software-register slots immediately below that frame.
     */
    *runContextOut = (RUN_SIZE)(RUN_UINTPTR_TYPE)(runFrame + 8);
    return RUN_STATUS_OK;
}

RUN_VOID run_arch_context_destroy(RUN_SIZE runContext)
{
    (RUN_VOID)runContext;
}

RUN_BOOL run_arch_context_requires_deferred_release(RUN_VOID)
{
    return RUN_FALSE;
}

RunCriticalState run_arch_enter_critical(RUN_VOID)
{
    RUN_UINT_32 runState;
    /* PRIMASK is the nesting token; restore the caller's previous value exactly. */
    __asm__ __volatile__("mrs %0, primask\n cpsid i" : "=r"(runState) : : "memory");
    return runState;
}

RUN_VOID run_arch_exit_critical(RunCriticalState runState)
{
    __asm__ __volatile__("msr primask, %0" : : "r"(runState) : "memory");
}

RUN_VOID run_arch_trigger_context_switch(RUN_VOID)
{
    RUN_SCB_ICSR = RUN_SCB_ICSR_PENDSVSET;
    RUN_BARRIER();
}

RUN_VOID run_arch_start_scheduler(RUN_VOID)
{
    /* PendSV performs the MSP-to-PSP transition and restores the first thread. */
    RUN_UINTPTR_TYPE runStackPointer = (RUN_UINTPTR_TYPE)RunKernelCurrent->runContext;
    __asm__ __volatile__("msr psp, %0\n isb" : : "r"(runStackPointer) : "memory");
    RUN_SCB_ICSR = RUN_SCB_ICSR_PENDSVSET;
    RUN_BARRIER();
}

RUN_VOID run_arch_idle(RUN_VOID)
{
    __asm__ __volatile__("wfi");
}

RUN_UINT_32 run_arch_get_ipsr(RUN_VOID)
{
    RUN_UINT_32 runValue;
    __asm__ __volatile__("mrs %0, ipsr" : "=r"(runValue));
    return runValue;
}

RUN_UINT_32 run_arch_get_cycle_count(RUN_VOID)
{
    /* DWT wraps naturally; elapsed-time checks use unsigned subtraction. */
    return RUN_DWT_CYCCNT;
}

RUN_VOID run_arch_enable_irq(RUN_UINT_32 runIrq)
{
    /* NVIC uses one bit per IRQ packed into 32-bit set/clear registers. */
    RUN_VOLATILE RUN_UINT_32 *runEnableArray =
        (RUN_VOLATILE RUN_UINT_32 *)0xE000E100U;
    runEnableArray[runIrq >> 5U] = 1UL << (runIrq & 31U);
}

RUN_VOID run_arch_disable_irq(RUN_UINT_32 runIrq)
{
    RUN_VOLATILE RUN_UINT_32 *runDisableArray =
        (RUN_VOLATILE RUN_UINT_32 *)0xE000E180U;
    runDisableArray[runIrq >> 5U] = 1UL << (runIrq & 31U);
}

RUN_VOID run_arch_set_irq_priority(RUN_UINT_32 runIrq, RUN_UINT_8 runPriority)
{
    RUN_VOLATILE RUN_UINT_8 *runPriorityArray =
        (RUN_VOLATILE RUN_UINT_8 *)0xE000E400U;
    runPriorityArray[runIrq] = runPriority;
}

RUN_ATTR_NORETURN RUN_VOID run_arch_system_reset(RUN_VOID)
{
    /* AIRCR requires the VECTKEY pattern or writes are ignored. */
    RUN_SCB_AIRCR = RUN_SCB_AIRCR_VECTKEY | RUN_SCB_AIRCR_SYSRESETREQ;
    RUN_BARRIER();
    for (;;)
    {
    }
}

