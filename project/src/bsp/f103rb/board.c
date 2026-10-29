/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - STM32F103RBT6 board startup
 *
 * Layer: bsp/f103rb
 * Purpose: Configure 72 MHz clocks, board pins, memory, kernel, and controllers.
 * Dependencies: STM32F1 drivers, Cortex-M3 port, and board public interface.
 * Constraint: All register setup happens before run_board_start_tick; clocks and
 *             device handles are fixed for the lifetime of this board port.
 */
#include "runrtos/arch/run_arch.h"
#include "runrtos/bsp/run_board.h"
#include "runrtos/core/run_assert.h"
#include "runrtos/core/run_config.h"
#include "runrtos/kernel/run_kernel.h"
#include "runrtos/runtime/run_memory.h"

#define RUN_F103_FLASH_ACR (*(RUN_VOLATILE RUN_UINT_32 *)0x40022000U)
#define RUN_F103_FLASH_ACR_LATENCY_2 (1UL << 1U)
#define RUN_F103_FLASH_ACR_PREFETCH (1UL << 4U)
#define RUN_F103_RCC_CR_HSEON (1UL << 16U)
#define RUN_F103_RCC_CR_HSERDY (1UL << 17U)
#define RUN_F103_RCC_CR_PLLON (1UL << 24U)
#define RUN_F103_RCC_CR_PLLRDY (1UL << 25U)
#define RUN_F103_RCC_CFGR_PLLMUL_X9 (7UL << 18U)
#define RUN_F103_RCC_CFGR_PLLMUL_MASK (0xFUL << 18U)
#define RUN_F103_RCC_CFGR_PLLSRC_HSE (1UL << 16U)
#define RUN_F103_RCC_CFGR_SW_PLL (2UL << 0U)
#define RUN_F103_RCC_CFGR_SW_MASK (3UL << 0U)
#define RUN_F103_RCC_CFGR_SWS_PLL (2UL << 2U)
#define RUN_F103_RCC_APB2_CLOCK_AFIO (1UL << 0U)
#define RUN_F103_RCC_APB2_CLOCK_GPIOA (1UL << 2U)
#define RUN_F103_RCC_APB2_CLOCK_GPIOB (1UL << 3U)
#define RUN_F103_RCC_APB2_CLOCK_GPIOC (1UL << 4U)
#define RUN_F103_RCC_APB2_CLOCK_GPIOD (1UL << 5U)
#define RUN_F103_RCC_APB2_CLOCK_USART1 (1UL << 14U)
#define RUN_F103_RCC_APB1_CLOCK_SPI2 (1UL << 14U)
#define RUN_F103_RCC_APB1_CLOCK_I2C1 (1UL << 21U)
#define RUN_F103_SYSTICK_CTRL (*(RUN_VOLATILE RUN_UINT_32 *)0xE000E010U)
#define RUN_F103_SYSTICK_LOAD (*(RUN_VOLATILE RUN_UINT_32 *)0xE000E014U)
#define RUN_F103_SYSTICK_VAL (*(RUN_VOLATILE RUN_UINT_32 *)0xE000E018U)
#define RUN_F103_SYSTICK_ENABLE (1UL << 0U)
#define RUN_F103_SYSTICK_TICKINT (1UL << 1U)
#define RUN_F103_SYSTICK_CLKSOURCE (1UL << 2U)
#define RUN_F103_SYSTEM_CLOCK_HZ 72000000U
#define RUN_F103_WAIT_LIMIT 1000000U

typedef RUN_STRUCT RunF103Rcc
{
    RUN_VOLATILE RUN_UINT_32 runClockControl;
    RUN_VOLATILE RUN_UINT_32 runClockConfiguration;
    RUN_VOLATILE RUN_UINT_32 runClockInterrupt;
    RUN_VOLATILE RUN_UINT_32 runApb2Reset;
    RUN_VOLATILE RUN_UINT_32 runApb1Reset;
    RUN_VOLATILE RUN_UINT_32 runAhbEnable;
    RUN_VOLATILE RUN_UINT_32 runApb2Enable;
    RUN_VOLATILE RUN_UINT_32 runApb1Enable;
    RUN_VOLATILE RUN_UINT_32 runBackupDomain;
    RUN_VOLATILE RUN_UINT_32 runControlStatus;
} RunF103Rcc;

RUN_STATIC RunF103Rcc *RUN_CONST RunF103RccInstance =
    (RunF103Rcc *)(RUN_VOID *)0x40021000U;
RUN_STATIC RUN_ATTR_ALIGNED(8) RUN_UINT_8 RunBoardMemoryArena[
    RUN_CONFIG_MEMORY_ARENA_BYTES
] RUN_ATTR_SECTION(".run_memory");
RUN_STATIC RunMemory *RunBoardMemory;
RUN_STATIC RUN_VOLATILE RUN_UINT_32 RunBoardKeyPresses;

/* Polls a clock-ready flag until it asserts or the bounded wait expires. */
RUN_STATIC RUN_BOOL run_f103_wait_flag(RUN_VOLATILE RUN_UINT_32 *runRegister,
                                       RUN_UINT_32 runMask)
{
    for (RUN_UINT_32 runCount = 0U; runCount < RUN_F103_WAIT_LIMIT; runCount++)
    {
        if ((*runRegister & runMask) != 0U)
        {
            return RUN_TRUE;
        }
    }
    return RUN_FALSE;
}

/* Configures the F103 PLL to source 72 MHz from the board's 8 MHz crystal. */
RUN_STATIC RunStatus run_f103_clock_init(RUN_VOID)
{
    /* 72 MHz exceeds the Flash zero-wait limit, so use two wait states. */
    RUN_F103_FLASH_ACR = RUN_F103_FLASH_ACR_LATENCY_2 |
                         RUN_F103_FLASH_ACR_PREFETCH;
    /* Bring up 8 MHz HSE, then configure PLL = HSE x 9 = 72 MHz. */
    RunF103RccInstance->runClockControl |= RUN_F103_RCC_CR_HSEON;
    if (!run_f103_wait_flag(&RunF103RccInstance->runClockControl,
                            RUN_F103_RCC_CR_HSERDY))
    {
        return RUN_STATUS_TIMEOUT;
    }
    RunF103RccInstance->runClockConfiguration =
        (RunF103RccInstance->runClockConfiguration &
         ~(RUN_F103_RCC_CFGR_PLLMUL_MASK | RUN_F103_RCC_CFGR_PLLSRC_HSE)) |
        RUN_F103_RCC_CFGR_PLLMUL_X9 | RUN_F103_RCC_CFGR_PLLSRC_HSE;
    RunF103RccInstance->runClockControl |= RUN_F103_RCC_CR_PLLON;
    if (!run_f103_wait_flag(&RunF103RccInstance->runClockControl,
                            RUN_F103_RCC_CR_PLLRDY))
    {
        return RUN_STATUS_TIMEOUT;
    }
    /* Switch SYSCLK only after the PLL reports lock, then verify the mux. */
    RunF103RccInstance->runClockConfiguration =
        (RunF103RccInstance->runClockConfiguration & ~RUN_F103_RCC_CFGR_SW_MASK) |
        RUN_F103_RCC_CFGR_SW_PLL;
    if (!run_f103_wait_flag(&RunF103RccInstance->runClockConfiguration,
                            RUN_F103_RCC_CFGR_SWS_PLL))
    {
        return RUN_STATUS_TIMEOUT;
    }
    return RUN_STATUS_OK;
}

/* Counts board key edges from the GPIO interrupt context. */
RUN_STATIC RUN_VOID run_f103_key_interrupt(RunDevice *runDevice,
                                           RUN_UINT_16 runPin,
                                           RUN_VOID *runArgument)
{
    (RUN_VOID)runDevice;
    (RUN_VOID)runPin;
    (RUN_VOID)runArgument;
    RunBoardKeyPresses++;
}

/* Maps the board's LED, console, buses, Flash, and key pins to device modes. */
RUN_STATIC RunStatus run_f103_configure_pins(RunDevice *runGpioA,
                                             RunDevice *runGpioB,
                                             RunDevice *runGpioC,
                                             RunDevice *runGpioD)
{
    RunStatus runStatus;
    /* PC0 drives the active-low user LED. */
    runStatus = run_gpio_configure(runGpioC, 0U, RUN_GPIO_MODE_OUTPUT_PUSH_PULL,
                                   RUN_GPIO_PULL_NONE);
    if (runStatus == RUN_STATUS_OK)
    {
        /* Start with the active-low LED off. */
        runStatus = run_gpio_write(runGpioC, 0U, RUN_TRUE);
    }
    /* PA9/PA10 are USART1 TX/RX. */
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_configure(runGpioA, 9U,
                                       RUN_GPIO_MODE_ALTERNATE_PUSH_PULL,
                                       RUN_GPIO_PULL_NONE);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_configure(runGpioA, 10U, RUN_GPIO_MODE_INPUT,
                                       RUN_GPIO_PULL_UP);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        /* PB6/PB7 are I2C1 open-drain SCL/SDA. */
        runStatus = run_gpio_configure(runGpioB, 6U,
                                       RUN_GPIO_MODE_ALTERNATE_OPEN_DRAIN,
                                       RUN_GPIO_PULL_NONE);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_configure(runGpioB, 7U,
                                       RUN_GPIO_MODE_ALTERNATE_OPEN_DRAIN,
                                       RUN_GPIO_PULL_NONE);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        /* PB12 is the SPI Flash chip-select. */
        runStatus = run_gpio_configure(runGpioB, 12U,
                                       RUN_GPIO_MODE_OUTPUT_PUSH_PULL,
                                       RUN_GPIO_PULL_NONE);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        /* CS starts deasserted (high), keeping the Flash deselected. */
        runStatus = run_gpio_write(runGpioB, 12U, RUN_TRUE);
    }
    /* PB13/PB14/PB15 are SPI2 SCK/MISO/MOSI alternate functions. */
    for (RUN_UINT_16 runPin = 13U; (runPin <= 15U) && (runStatus == RUN_STATUS_OK);
         runPin++)
    {
        runStatus = run_gpio_configure(runGpioB, runPin,
                                       RUN_GPIO_MODE_ALTERNATE_PUSH_PULL,
                                       RUN_GPIO_PULL_NONE);
    }
    /* WK_UP is active high; KEY0/KEY1/KEY2 are active low. */
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_configure(runGpioA, 0U, RUN_GPIO_MODE_INPUT,
                                       RUN_GPIO_PULL_DOWN);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_configure(runGpioC, 8U, RUN_GPIO_MODE_INPUT,
                                       RUN_GPIO_PULL_UP);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_configure(runGpioC, 9U, RUN_GPIO_MODE_INPUT,
                                       RUN_GPIO_PULL_UP);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_configure(runGpioD, 2U, RUN_GPIO_MODE_INPUT,
                                       RUN_GPIO_PULL_UP);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_attach_interrupt(
            runGpioA, 0U, RUN_TRUE, RUN_FALSE,
            run_f103_key_interrupt, RUN_NULL);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_attach_interrupt(
            runGpioC, 8U, RUN_FALSE, RUN_TRUE,
            run_f103_key_interrupt, RUN_NULL);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_attach_interrupt(
            runGpioC, 9U, RUN_FALSE, RUN_TRUE,
            run_f103_key_interrupt, RUN_NULL);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_attach_interrupt(
            runGpioD, 2U, RUN_FALSE, RUN_TRUE,
            run_f103_key_interrupt, RUN_NULL);
    }
    return runStatus;
}

/* Creates core devices and initializes kernel services for the application. */
RunStatus run_board_init(RunBoardDevices *runDevicesOut)
{
    RunDevice *runGpioA = RUN_NULL;
    RunDevice *runGpioB = RUN_NULL;
    RunDevice *runGpioC = RUN_NULL;
    RunDevice *runGpioD = RUN_NULL;
    RunStatus runStatus;

    if (runDevicesOut == RUN_NULL)
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    run_arch_early_init();
    runStatus = run_f103_clock_init();
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    /* Clocks must be enabled before the first peripheral register access. */
    RunF103RccInstance->runApb2Enable |=
        RUN_F103_RCC_APB2_CLOCK_AFIO | RUN_F103_RCC_APB2_CLOCK_GPIOA |
        RUN_F103_RCC_APB2_CLOCK_GPIOB | RUN_F103_RCC_APB2_CLOCK_GPIOC |
        RUN_F103_RCC_APB2_CLOCK_GPIOD |
        RUN_F103_RCC_APB2_CLOCK_USART1;
    RunF103RccInstance->runApb1Enable |=
        RUN_F103_RCC_APB1_CLOCK_SPI2 | RUN_F103_RCC_APB1_CLOCK_I2C1;

    /* All later objects come from this single board-owned arena. */
    runStatus = run_memory_init(&RunBoardMemory, RunBoardMemoryArena,
                                sizeof(RunBoardMemoryArena));
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_kernel_init(RunBoardMemory);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_create_f103(0U, "gpio-a", &runGpioA);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_create_f103(1U, "gpio-b", &runGpioB);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_create_f103(2U, "gpio-c", &runGpioC);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_gpio_create_f103(3U, "gpio-d", &runGpioD);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_f103_configure_pins(runGpioA, runGpioB, runGpioC,
                                            runGpioD);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_uart_create_f103(1U, "uart1", &runDevicesOut->runUart1);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_uart_configure(runDevicesOut->runUart1, 115200U, 8U, 0U, 1U);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_spi_create_f103(2U, "spi2", &runDevicesOut->runSpi2);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_i2c_create_f103(1U, "i2c1", &runDevicesOut->runI2c1);
    }
    if (runStatus == RUN_STATUS_OK)
    {
        runDevicesOut->runMemory = RunBoardMemory;
        runDevicesOut->runLedPort = runGpioC;
        runDevicesOut->runGpioA = runGpioA;
        runDevicesOut->runGpioB = runGpioB;
        runDevicesOut->runGpioC = runGpioC;
        runDevicesOut->runGpioD = runGpioD;
    }
    return runStatus;
}

/* Programs SysTick for the configured scheduler frequency and starts tick IRQs. */
RunStatus run_board_start_tick(RUN_VOID)
{
    /* SysTick uses the 72 MHz core clock and counts down from reload to zero. */
    RUN_UINT_32 runReload = (RUN_F103_SYSTEM_CLOCK_HZ / RUN_CONFIG_TICK_HZ) - 1U;
    if ((RUN_CONFIG_TICK_HZ == 0U) || (runReload == 0U))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    /* SysTick exception 15 shares the lowest priority with PendSV. */
    run_arch_set_irq_priority(15U, 0xFFU);
    RUN_F103_SYSTICK_LOAD = runReload;
    RUN_F103_SYSTICK_VAL = 0U;
    RUN_F103_SYSTICK_CTRL = RUN_F103_SYSTICK_CLKSOURCE |
                            RUN_F103_SYSTICK_TICKINT |
                            RUN_F103_SYSTICK_ENABLE;
    return RUN_STATUS_OK;
}

/* Returns the accumulated key-edge count for diagnostics. */
RUN_UINT_32 run_board_get_key_press_count(RUN_VOID)
{
    return RunBoardKeyPresses;
}

