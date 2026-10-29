/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - freestanding diagnostic formatter
 *
 * Layer: runtime
 * Purpose: Format common integer, pointer, character, and string fields without libc.
 * Dependencies: memory and string runtime services.
 * Constraint: Output is emitted incrementally; the bounded buffer writer records
 *             truncation while preserving a terminating NUL.
 */
#include "runrtos/runtime/run_format.h"

typedef RUN_STRUCT RunFormatContext
{
    RunFormatWrite runWrite;
    RUN_VOID *runUserContext;
    RUN_SIZE runWritten;
} RunFormatContext;

RUN_STATIC RUN_VOID run_format_emit(RunFormatContext *runContext,
                                   RUN_CONST RUN_CHAR *runData,
                                   RUN_SIZE runLength)
{
    if ((runContext->runWrite != RUN_NULL) && (runLength != 0U))
    {
        runContext->runWrite(runData, runLength, runContext->runUserContext);
    }
    runContext->runWritten += runLength;
}

RUN_STATIC RUN_VOID run_format_repeat(RunFormatContext *runContext,
                                     RUN_CHAR runCharacter,
                                     RUN_SIZE runCount)
{
    RUN_CHAR runBuffer[8];
    run_memory_set(runBuffer, (RUN_INT_32)runCharacter, sizeof(runBuffer));
    while (runCount > 0U)
    {
        RUN_SIZE runChunk = RUN_MIN(runCount, sizeof(runBuffer));
        run_format_emit(runContext, runBuffer, runChunk);
        runCount -= runChunk;
    }
}

RUN_STATIC RUN_VOID run_format_unsigned(RunFormatContext *runContext,
                                       RUN_UINT_64 runValue,
                                       RUN_UINT_32 runBase,
                                       RUN_BOOL runUppercase,
                                       RUN_UINT_32 runWidth,
                                       RUN_CHAR runPad)
{
    RUN_CHAR runBuffer[24];
    RUN_SIZE runLength = 0U;
    RUN_CONST RUN_CHAR *runDigits = runUppercase ?
        "0123456789ABCDEF" : "0123456789abcdef";

    /* Generate least-significant digit first, then emit the temporary in reverse. */
    do
    {
        runBuffer[runLength++] = runDigits[(RUN_UINT_32)(runValue % runBase)];
        runValue /= runBase;
    } while ((runValue != 0U) && (runLength < sizeof(runBuffer)));

    if (runWidth > runLength)
    {
        run_format_repeat(runContext, runPad, runWidth - runLength);
    }
    while (runLength > 0U)
    {
        run_format_emit(runContext, &runBuffer[--runLength], 1U);
    }
}

RUN_STATIC RUN_VOID run_format_signed(RunFormatContext *runContext,
                                     RUN_INT_64 runValue,
                                     RUN_UINT_32 runWidth,
                                     RUN_CHAR runPad)
{
    RUN_UINT_64 runMagnitude;
    RUN_BOOL runNegative = (runValue < 0) ? RUN_TRUE : RUN_FALSE;
    /* -(INT64_MIN) would overflow; convert through its unsigned magnitude safely. */
    if (runNegative)
    {
        runMagnitude = (RUN_UINT_64)(-(runValue + 1)) + 1U;
    }
    else
    {
        runMagnitude = (RUN_UINT_64)runValue;
    }
    if (runNegative)
    {
        run_format_emit(runContext, "-", 1U);
        if (runWidth > 0U)
        {
            runWidth--;
        }
    }
    run_format_unsigned(runContext, runMagnitude, 10U, RUN_FALSE, runWidth, runPad);
}

RunStatus run_format(RunFormatWrite runWrite,
                     RUN_VOID *runContext,
                     RUN_CONST RUN_CHAR *runFormat,
                     RUN_VA_LIST runArguments)
{
    RunFormatContext runFormatContext = { runWrite, runContext, 0U };
    if ((runFormat == RUN_NULL) || (runWrite == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }

    while (*runFormat != '\0')
    {
        if (*runFormat != '%')
        {
            RUN_CONST RUN_CHAR *runStart = runFormat;
            while ((*runFormat != '\0') && (*runFormat != '%'))
            {
                runFormat++;
            }
            run_format_emit(&runFormatContext, runStart, (RUN_SIZE)(runFormat - runStart));
            continue;
        }

        runFormat++;
        RUN_CHAR runPad = ' ';
        RUN_UINT_32 runWidth = 0U;
        RUN_BOOL runLongLong = RUN_FALSE;
        RUN_BOOL runSizeArgument = RUN_FALSE;
        if (*runFormat == '0')
        {
            runPad = '0';
            runFormat++;
        }
        while ((*runFormat >= '0') && (*runFormat <= '9'))
        {
            runWidth = (runWidth * 10U) + (RUN_UINT_32)(*runFormat - '0');
            runFormat++;
        }
        if (*runFormat == 'z')
        {
            runSizeArgument = RUN_TRUE;
            runFormat++;
        }
        else if ((runFormat[0] == 'l') && (runFormat[1] == 'l'))
        {
            runLongLong = RUN_TRUE;
            runFormat += 2;
        }
        else if (*runFormat == 'l')
        {
            runLongLong = RUN_TRUE;
            runFormat++;
        }

        switch (*runFormat)
        {
        case '%':
            run_format_emit(&runFormatContext, "%", 1U);
            break;
        case 'c':
        {
            RUN_CHAR runCharacter = (RUN_CHAR)RUN_VA_ARG(runArguments, RUN_INT_32);
            run_format_emit(&runFormatContext, &runCharacter, 1U);
            break;
        }
        case 's':
        {
            RUN_CONST RUN_CHAR *runText = RUN_VA_ARG(runArguments, RUN_CONST RUN_CHAR *);
            RUN_SIZE runLength = run_string_length(runText);
            if (runWidth > runLength)
            {
                run_format_repeat(&runFormatContext, ' ', runWidth - runLength);
            }
            run_format_emit(&runFormatContext, runText, runLength);
            break;
        }
        case 'd':
        case 'i':
        {
            RUN_INT_64 runValue = runLongLong ?
                RUN_VA_ARG(runArguments, RUN_INT_64) :
                (RUN_INT_64)RUN_VA_ARG(runArguments, RUN_INT_32);
            run_format_signed(&runFormatContext, runValue, runWidth, runPad);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        {
            RUN_UINT_64 runValue;
            if (runSizeArgument)
            {
                runValue = (RUN_UINT_64)RUN_VA_ARG(runArguments, RUN_SIZE);
            }
            else if (runLongLong)
            {
                runValue = RUN_VA_ARG(runArguments, RUN_UINT_64);
            }
            else
            {
                runValue = (RUN_UINT_64)RUN_VA_ARG(runArguments, RUN_UINT_32);
            }
            run_format_unsigned(&runFormatContext, runValue,
                                (*runFormat == 'u') ? 10U : 16U,
                                (*runFormat == 'X') ? RUN_TRUE : RUN_FALSE,
                                runWidth, runPad);
            break;
        }
        case 'p':
        {
            RUN_UINTPTR_TYPE runPointer =
                (RUN_UINTPTR_TYPE)RUN_VA_ARG(runArguments, RUN_VOID *);
            run_format_emit(&runFormatContext, "0x", 2U);
            run_format_unsigned(&runFormatContext, (RUN_UINT_64)runPointer, 16U,
                                RUN_FALSE, (RUN_UINT_32)(sizeof(RUN_VOID *) * 2U), '0');
            break;
        }
        case '\0':
            return RUN_STATUS_INVALID_ARGUMENT;
        default:
            /* Preserve unknown conversions for forward-compatible diagnostics. */
            run_format_emit(&runFormatContext, "%", 1U);
            run_format_emit(&runFormatContext, runFormat, 1U);
            break;
        }
        if (*runFormat != '\0')
        {
            runFormat++;
        }
    }
    return RUN_STATUS_OK;
}

typedef RUN_STRUCT RunFormatBufferContext
{
    RUN_CHAR *runBuffer;
    RUN_SIZE runCapacity;
    RUN_SIZE runStored;
    RUN_BOOL runOverflow;
} RunFormatBufferContext;

RUN_STATIC RUN_VOID run_format_buffer_write(RUN_CONST RUN_CHAR *runData,
                                            RUN_SIZE runLength,
                                            RUN_VOID *runContext)
{
    RunFormatBufferContext *runBufferContext = (RunFormatBufferContext *)runContext;
    /* One byte is always reserved for the terminator, even after truncation. */
    RUN_SIZE runAvailable = (runBufferContext->runCapacity > runBufferContext->runStored) ?
        (runBufferContext->runCapacity - runBufferContext->runStored - 1U) : 0U;
    RUN_SIZE runCopy = RUN_MIN(runAvailable, runLength);
    if (runCopy != 0U)
    {
        run_memory_copy(runBufferContext->runBuffer + runBufferContext->runStored,
                        runData, runCopy);
        runBufferContext->runStored += runCopy;
    }
    if (runCopy < runLength)
    {
        runBufferContext->runOverflow = RUN_TRUE;
    }
}

RunStatus run_format_buffer(RUN_CHAR *runBuffer,
                            RUN_SIZE runBufferSize,
                            RUN_SIZE *runLengthOut,
                            RUN_CONST RUN_CHAR *runFormat,
                            ...)
{
    RunFormatBufferContext runContext;
    RUN_VA_LIST runArguments;
    RunStatus runStatus;

    if ((runBuffer == RUN_NULL) || (runBufferSize == 0U) || (runFormat == RUN_NULL))
    {
        return RUN_STATUS_INVALID_ARGUMENT;
    }
    runContext.runBuffer = runBuffer;
    runContext.runCapacity = runBufferSize;
    runContext.runStored = 0U;
    runContext.runOverflow = RUN_FALSE;
    RUN_VA_START(runArguments, runFormat);
    runStatus = run_format(run_format_buffer_write, &runContext, runFormat, runArguments);
    RUN_VA_END(runArguments);
    runBuffer[runContext.runStored] = '\0';
    if (runLengthOut != RUN_NULL)
    {
        *runLengthOut = runContext.runStored;
    }
    if ((runStatus == RUN_STATUS_OK) && runContext.runOverflow)
    {
        runStatus = RUN_STATUS_OVERFLOW;
    }
    return runStatus;
}

RUN_UINT_64 run_format_parse_u64(RUN_CONST RUN_CHAR *runText, RUN_BOOL *runValidOut)
{
    RUN_UINT_64 runValue = 0U;
    RUN_BOOL runValid = RUN_FALSE;
    if (runText != RUN_NULL)
    {
        while ((*runText >= '0') && (*runText <= '9'))
        {
            runValue = (runValue * 10U) + (RUN_UINT_64)(*runText - '0');
            runText++;
            runValid = RUN_TRUE;
        }
        /* Reject trailing junk instead of silently accepting a numeric prefix. */
        if (*runText != '\0')
        {
            runValid = RUN_FALSE;
        }
    }
    if (runValidOut != RUN_NULL)
    {
        *runValidOut = runValid;
    }
    return runValue;
}

RUN_BOOL run_format_parse_u32(RUN_CONST RUN_CHAR *runText, RUN_UINT_32 *runValueOut)
{
    RUN_BOOL runValid;
    RUN_UINT_64 runValue = run_format_parse_u64(runText, &runValid);
    if (runValue > 0xFFFFFFFFU)
    {
        runValid = RUN_FALSE;
    }
    if (runValueOut != RUN_NULL)
    {
        *runValueOut = (RUN_UINT_32)runValue;
    }
    return runValid;
}

