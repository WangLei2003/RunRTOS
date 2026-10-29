/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - STM32F103RBT6 board interface
 *
 * Layer: bsp
 * Purpose: Initialize clocks and pins and expose board-level device handles.
 * Dependencies: generic driver interfaces and the STM32F1 SOC target.
 * Constraint: run_board_init must run after reset but before the scheduler;
 *             returned device handles remain owned by the board.
 */
#ifndef RUN_BOARD_H_
#define RUN_BOARD_H_

#include "runrtos/drivers/gpio/run_gpio.h"
#include "runrtos/drivers/i2c/run_i2c.h"
#include "runrtos/drivers/spi/run_spi.h"
#include "runrtos/drivers/uart/run_uart.h"

typedef RUN_STRUCT RunBoardDevices
{
    /* All handles are board-owned and must not be deleted by applications. */
    RunDevice *runLedPort;
    RunDevice *runUart1;
    RunDevice *runSpi2;
    RunDevice *runI2c1;
} RunBoardDevices;

/*
 * Initializes 72 MHz clocks, the board arena, kernel, GPIO, USART1, SPI2, and
 * I2C1 in that order. On failure, no rollback is attempted and the output remains
 * partially initialized; callers should treat failure as fatal during bring-up.
 */
RunStatus run_board_init(RunBoardDevices *runDevicesOut);

/* Starts the 1 kHz SysTick source after all boot-time objects have been created. */
RunStatus run_board_start_tick(RUN_VOID);

#endif

