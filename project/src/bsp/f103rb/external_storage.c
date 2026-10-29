/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - F103RB external storage verification
 *
 * Layer: bsp/f103rb
 * Purpose: Access the board's 24C02 EEPROM and reversibly test W25Q16 sector zero.
 * Dependencies: board device handles, SPI/I2C drivers, kernel sleep, and memory.
 * Constraint: Call from thread context only; no DMA, cache, or libc is used.
 */
#include "runrtos/bsp/run_board.h"
#include "runrtos/kernel/run_kernel.h"

#define RUN_F103_EEPROM_ADDRESS 0x50U
#define RUN_F103_EEPROM_WRITE_CYCLES_MS 6U
#define RUN_F103_FLASH_SECTOR_BYTES 4096U
#define RUN_F103_FLASH_PAGE_BYTES 256U
#define RUN_F103_FLASH_JEDEC_WINBOND 0xEF4015U
#define RUN_F103_FLASH_JEDEC_BOYA 0x684015U
#define RUN_F103_FLASH_IO_TIMEOUT_MS 100U
#define RUN_F103_FLASH_READY_TIMEOUT_MS 2500U
#define RUN_F103_FLASH_CMD_WRITE_ENABLE 0x06U
#define RUN_F103_FLASH_CMD_READ_STATUS 0x05U
#define RUN_F103_FLASH_CMD_READ_DATA 0x03U
#define RUN_F103_FLASH_CMD_PAGE_PROGRAM 0x02U
#define RUN_F103_FLASH_CMD_SECTOR_ERASE 0x20U
#define RUN_F103_FLASH_CMD_JEDEC_ID 0x9FU
#define RUN_F103_FLASH_STATUS_BUSY 0x01U

/* Drives the W25Q16 chip-select pin to its active-low state. */
RUN_STATIC RUN_VOID run_f103_flash_chip_select(RunBoardDevices *runDevices,
                                               RUN_BOOL runSelected)
{
    (RUN_VOID)run_gpio_write(runDevices->runGpioB, 12U, runSelected ?
                             RUN_FALSE : RUN_TRUE);
}

/* Performs one raw full-duplex SPI transfer for an external-memory command. */
RUN_STATIC RunStatus run_f103_flash_transfer(
    RunBoardDevices *runDevices,
    RUN_CONST RUN_UINT_8 *runTransmit,
    RUN_UINT_8 *runReceive,
    RUN_SIZE runLength)
{
    return run_spi_transfer(runDevices->runSpi2, runTransmit, runReceive,
                            runLength, RUN_F103_FLASH_IO_TIMEOUT_MS);
}

/* Waits for the W25Q16 busy bit to clear within a bounded one-second window. */
RUN_STATIC RunStatus run_f103_flash_wait_ready(RunBoardDevices *runDevices)
{
    RUN_UINT_8 runCommand = RUN_F103_FLASH_CMD_READ_STATUS;
    RUN_UINT_8 runStatus;
    for (RUN_UINT_32 runAttempt = 0U;
         runAttempt < RUN_F103_FLASH_READY_TIMEOUT_MS; runAttempt++)
    {
        RunStatus runResult;
        run_f103_flash_chip_select(runDevices, RUN_TRUE);
        runResult = run_f103_flash_transfer(runDevices, &runCommand,
                                            &runStatus, 1U);
        if (runResult == RUN_STATUS_OK)
        {
            runResult = run_f103_flash_transfer(runDevices, RUN_NULL,
                                                &runStatus, 1U);
        }
        run_f103_flash_chip_select(runDevices, RUN_FALSE);
        if (runResult != RUN_STATUS_OK)
        {
            return runResult;
        }
        if ((runStatus & RUN_F103_FLASH_STATUS_BUSY) == 0U)
        {
            return RUN_STATUS_OK;
        }
        (RUN_VOID)run_thread_sleep(1U);
    }
    return RUN_STATUS_TIMEOUT;
}

/* Sends a one-byte command to the external SPI Flash. */
RUN_STATIC RunStatus run_f103_flash_command(RunBoardDevices *runDevices,
                                            RUN_UINT_8 runCommand)
{
    RunStatus runResult;
    run_f103_flash_chip_select(runDevices, RUN_TRUE);
    runResult = run_f103_flash_transfer(runDevices, &runCommand, RUN_NULL, 1U);
    run_f103_flash_chip_select(runDevices, RUN_FALSE);
    return runResult;
}

/* Erases W25Q16 sector zero and waits for the erase to complete. */
RUN_STATIC RunStatus run_f103_flash_erase_sector_zero(
    RunBoardDevices *runDevices)
{
    RUN_UINT_8 runCommand[4] = {
        RUN_F103_FLASH_CMD_SECTOR_ERASE, 0U, 0U, 0U
    };
    RunStatus runResult = run_f103_flash_command(
        runDevices, RUN_F103_FLASH_CMD_WRITE_ENABLE);
    if (runResult == RUN_STATUS_OK)
    {
        run_f103_flash_chip_select(runDevices, RUN_TRUE);
        runResult = run_f103_flash_transfer(runDevices, runCommand, RUN_NULL, 4U);
        run_f103_flash_chip_select(runDevices, RUN_FALSE);
    }
    if (runResult == RUN_STATUS_OK)
    {
        runResult = run_f103_flash_wait_ready(runDevices);
    }
    return runResult;
}

/* Reads a bounded byte range through the W25Q16 read-data command. */
RUN_STATIC RunStatus run_f103_flash_read(
    RunBoardDevices *runDevices,
    RUN_UINT_32 runAddress,
    RUN_UINT_8 *runData,
    RUN_SIZE runLength)
{
    RUN_UINT_8 runCommand[4];
    RunStatus runResult;
    runCommand[0] = RUN_F103_FLASH_CMD_READ_DATA;
    runCommand[1] = (RUN_UINT_8)(runAddress >> 16U);
    runCommand[2] = (RUN_UINT_8)(runAddress >> 8U);
    runCommand[3] = (RUN_UINT_8)runAddress;
    run_f103_flash_chip_select(runDevices, RUN_TRUE);
    runResult = run_f103_flash_transfer(runDevices, runCommand, RUN_NULL, 4U);
    if (runResult == RUN_STATUS_OK)
    {
        runResult = run_f103_flash_transfer(runDevices, RUN_NULL, runData,
                                            runLength);
    }
    run_f103_flash_chip_select(runDevices, RUN_FALSE);
    return runResult;
}

/* Programs one complete Flash page and waits for the write cycle. */
RUN_STATIC RunStatus run_f103_flash_program_page(
    RunBoardDevices *runDevices,
    RUN_UINT_32 runAddress,
    RUN_CONST RUN_UINT_8 *runData)
{
    RUN_UINT_8 runCommand[4];
    RunStatus runResult = run_f103_flash_command(
        runDevices, RUN_F103_FLASH_CMD_WRITE_ENABLE);
    if (runResult != RUN_STATUS_OK)
    {
        return runResult;
    }
    runCommand[0] = RUN_F103_FLASH_CMD_PAGE_PROGRAM;
    runCommand[1] = (RUN_UINT_8)(runAddress >> 16U);
    runCommand[2] = (RUN_UINT_8)(runAddress >> 8U);
    runCommand[3] = (RUN_UINT_8)runAddress;
    run_f103_flash_chip_select(runDevices, RUN_TRUE);
    runResult = run_f103_flash_transfer(runDevices, runCommand, RUN_NULL, 4U);
    if (runResult == RUN_STATUS_OK)
    {
        runResult = run_f103_flash_transfer(runDevices, runData, RUN_NULL,
                                            RUN_F103_FLASH_PAGE_BYTES);
    }
    run_f103_flash_chip_select(runDevices, RUN_FALSE);
    if (runResult == RUN_STATUS_OK)
    {
        runResult = run_f103_flash_wait_ready(runDevices);
    }
    return runResult;
}

/* Produces a deterministic nonzero test byte for each sector offset. */
RUN_STATIC RUN_UINT_8 run_f103_flash_test_pattern(RUN_SIZE runOffset)
{
    return (RUN_UINT_8)(0xA5U ^ (RUN_UINT_8)(runOffset * 31U));
}

/* Accepts the 2 MiB 25Q16-compatible identities used by supported boards. */
RUN_STATIC RUN_BOOL run_f103_flash_jedec_supported(RUN_UINT_32 runJedec)
{
    return ((runJedec == RUN_F103_FLASH_JEDEC_WINBOND) ||
            (runJedec == RUN_F103_FLASH_JEDEC_BOYA)) ? RUN_TRUE : RUN_FALSE;
}

/* Compares a whole sector against a saved image or generated pattern. */
RUN_STATIC RunStatus run_f103_flash_verify_sector(
    RunBoardDevices *runDevices,
    RUN_CONST RUN_UINT_8 *runExpected,
    RUN_BOOL runUsePattern)
{
    RUN_UINT_8 runReadPage[RUN_F103_FLASH_PAGE_BYTES];
    for (RUN_SIZE runOffset = 0U; runOffset < RUN_F103_FLASH_SECTOR_BYTES;
         runOffset += RUN_F103_FLASH_PAGE_BYTES)
    {
        RunStatus runResult = run_f103_flash_read(
            runDevices, (RUN_UINT_32)runOffset, runReadPage,
            sizeof(runReadPage));
        if (runResult != RUN_STATUS_OK)
        {
            return runResult;
        }
        for (RUN_SIZE runIndex = 0U; runIndex < RUN_F103_FLASH_PAGE_BYTES;
             runIndex++)
        {
            RUN_UINT_8 runValue = runUsePattern ?
                run_f103_flash_test_pattern(runOffset + runIndex) :
                runExpected[runOffset + runIndex];
            if (runReadPage[runIndex] != runValue)
            {
                return RUN_STATUS_ERROR;
            }
        }
    }
    return RUN_STATUS_OK;
}

/* Writes one sector in page-sized chunks after erasing it. */
RUN_STATIC RunStatus run_f103_flash_program_sector(
    RunBoardDevices *runDevices,
    RUN_CONST RUN_UINT_8 *runData,
    RUN_BOOL runUsePattern)
{
    RUN_UINT_8 runPage[RUN_F103_FLASH_PAGE_BYTES];
    RunStatus runResult = run_f103_flash_erase_sector_zero(runDevices);
    if (runResult != RUN_STATUS_OK)
    {
        return runResult;
    }
    for (RUN_SIZE runOffset = 0U;
         (runOffset < RUN_F103_FLASH_SECTOR_BYTES) &&
         (runResult == RUN_STATUS_OK);
         runOffset += RUN_F103_FLASH_PAGE_BYTES)
    {
        for (RUN_SIZE runIndex = 0U; runIndex < RUN_F103_FLASH_PAGE_BYTES;
             runIndex++)
        {
            runPage[runIndex] = runUsePattern ?
                run_f103_flash_test_pattern(runOffset + runIndex) :
                runData[runOffset + runIndex];
        }
        runResult = run_f103_flash_program_page(
            runDevices, (RUN_UINT_32)runOffset, runPage);
    }
    return runResult;
}

/* Reads one byte from the board 24C02 at the requested EEPROM address. */
RunStatus run_board_24c02_read_byte(RunBoardDevices *runDevices,
                                    RUN_UINT_8 runOffset,
                                    RUN_UINT_8 *runValueOut)
{
    if ((runDevices == RUN_NULL) || (runDevices->runI2c1 == RUN_NULL) ||
        (runValueOut == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    return run_i2c_transaction(runDevices->runI2c1, RUN_F103_EEPROM_ADDRESS,
                               &runOffset, 1U, runValueOut, 1U,
                               RUN_F103_FLASH_IO_TIMEOUT_MS);
}

/* Writes one byte to the board 24C02 and waits out its self-timed cycle. */
RunStatus run_board_24c02_write_byte(RunBoardDevices *runDevices,
                                     RUN_UINT_8 runOffset,
                                     RUN_UINT_8 runValue)
{
    RUN_UINT_8 runData[2];
    RunStatus runResult;
    if ((runDevices == RUN_NULL) || (runDevices->runI2c1 == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runData[0] = runOffset;
    runData[1] = runValue;
    runResult = run_i2c_transaction(runDevices->runI2c1,
                                    RUN_F103_EEPROM_ADDRESS, runData, 2U,
                                    RUN_NULL, 0U,
                                    RUN_F103_FLASH_IO_TIMEOUT_MS);
    if (runResult == RUN_STATUS_OK)
    {
        /* EEPROM internally timed write cycle; no ACK is required during it. */
        (RUN_VOID)run_thread_sleep(RUN_F103_EEPROM_WRITE_CYCLES_MS);
    }
    return runResult;
}

/* Verifies the W25Q16 identity and reversibly exercises its first sector. */
RunStatus run_board_w25q16_selftest(RunBoardDevices *runDevices)
{
    RUN_UINT_8 runJedec[4] = {
        RUN_F103_FLASH_CMD_JEDEC_ID, 0xFFU, 0xFFU, 0xFFU
    };
    RUN_UINT_8 runJedecRead[4];
    RUN_UINT_8 *runBackup;
    RunStatus runStatus;
    RunStatus runRestoreStatus;
    RUN_BOOL runDestructiveStarted = RUN_FALSE;

    if ((runDevices == RUN_NULL) || (runDevices->runSpi2 == RUN_NULL) ||
        (runDevices->runMemory == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    run_f103_flash_chip_select(runDevices, RUN_TRUE);
    runStatus = run_f103_flash_transfer(runDevices, runJedec, runJedecRead, 4U);
    run_f103_flash_chip_select(runDevices, RUN_FALSE);
    if (runStatus != RUN_STATUS_OK)
    {
        return runStatus;
    }
    if (!run_f103_flash_jedec_supported(
            ((RUN_UINT_32)runJedecRead[1] << 16U) |
            ((RUN_UINT_32)runJedecRead[2] << 8U) |
            (RUN_UINT_32)runJedecRead[3]))
    {
        return RUN_STATUS_ERROR;
    }

    runStatus = run_memory_allocate(runDevices->runMemory,
                                    RUN_F103_FLASH_SECTOR_BYTES,
                                    RUN_MEMORY_ALIGNMENT,
                                    (RUN_VOID **)&runBackup);
    if (runStatus != RUN_STATUS_OK)
    {
        return (runStatus == RUN_STATUS_NO_MEMORY) ?
            RUN_STATUS_NO_MEMORY : runStatus;
    }
    runStatus = run_f103_flash_read(runDevices, 0U, runBackup,
                                    RUN_F103_FLASH_SECTOR_BYTES);
    if (runStatus != RUN_STATUS_OK)
    {
        (RUN_VOID)run_memory_release(runDevices->runMemory, runBackup);
        return runStatus;
    }

    /*
     * The backup is complete and trustworthy at this point. From the moment
     * erase is issued, every exit path must attempt to restore the sector.
     */
    runDestructiveStarted = RUN_TRUE;
    runStatus = run_f103_flash_program_sector(runDevices, RUN_NULL, RUN_TRUE);
    if (runStatus == RUN_STATUS_OK)
    {
        runStatus = run_f103_flash_verify_sector(runDevices, RUN_NULL, RUN_TRUE);
    }

    /*
     * A power failure in this window cannot be recovered by software.
     * Restore is deliberately skipped if the initial backup read failed.
     */
    runRestoreStatus = runDestructiveStarted ?
        run_f103_flash_program_sector(runDevices, runBackup, RUN_FALSE) :
        RUN_STATUS_OK;
    if (runRestoreStatus == RUN_STATUS_OK)
    {
        runRestoreStatus = run_f103_flash_verify_sector(runDevices, runBackup,
                                                        RUN_FALSE);
    }
    (RUN_VOID)run_memory_release(runDevices->runMemory, runBackup);
    return (runStatus != RUN_STATUS_OK) ? runStatus : runRestoreStatus;
}
