/*
 * SPDX-License-Identifier: Apache-2.0
 * RunRTOS - compiler and language adaptation
 *
 * Layer: core
 * Purpose: Centralize compiler attributes, predefined types, and varargs.
 * Dependencies: GCC/Clang predefined facilities only.
 * Constraint: This is the only public header allowed to expose such details.
 */
#ifndef RUN_COMPILER_H_
#define RUN_COMPILER_H_

#define RUN_CONST const
#define RUN_VOLATILE volatile
#define RUN_STATIC static
#define RUN_EXTERN extern
#define RUN_INLINE inline
#define RUN_REGISTER register
#define RUN_TYPEDEF typedef
#define RUN_STRUCT struct
#define RUN_UNION union
#define RUN_ENUM enum

#define RUN_VOID void
#define RUN_CHAR char
#define RUN_BOOL _Bool
#define RUN_FP_32 float
#define RUN_FP_64 double

RUN_TYPEDEF __INT8_TYPE__ RUN_INT_8;
RUN_TYPEDEF __UINT8_TYPE__ RUN_UINT_8;
RUN_TYPEDEF __INT16_TYPE__ RUN_INT_16;
RUN_TYPEDEF __UINT16_TYPE__ RUN_UINT_16;
RUN_TYPEDEF __INT32_TYPE__ RUN_INT_32;
RUN_TYPEDEF __UINT32_TYPE__ RUN_UINT_32;
RUN_TYPEDEF __INT64_TYPE__ RUN_INT_64;
RUN_TYPEDEF __UINT64_TYPE__ RUN_UINT_64;
RUN_TYPEDEF __SIZE_TYPE__ RUN_SIZE;
RUN_TYPEDEF __PTRDIFF_TYPE__ RUN_PTRDIFF;
RUN_TYPEDEF __UINTPTR_TYPE__ RUN_UINTPTR_TYPE;

#define RUN_TRUE 1
#define RUN_FALSE 0
#define RUN_NULL ((RUN_VOID *)0)
#define RUN_SIZE_MAX ((RUN_SIZE)-1)

#define RUN_ATTR_WEAK __attribute__((weak))
#define RUN_ATTR_NORETURN __attribute__((noreturn))
#define RUN_ATTR_UNUSED __attribute__((unused))
#define RUN_ATTR_ALIGNED(_runValue) __attribute__((aligned(_runValue)))
#define RUN_ATTR_SECTION(_runValue) __attribute__((section(_runValue)))
#define RUN_ATTR_PACKED __attribute__((packed))
#define RUN_ATTR_NAKED __attribute__((naked))
#define RUN_ATTR_USED __attribute__((used))
#define RUN_FORCE_INLINE RUN_STATIC RUN_INLINE __attribute__((always_inline))
#define RUN_NOINLINE __attribute__((noinline))
#define RUN_BARRIER() __asm__ __volatile__("" ::: "memory")

#define RUN_STATIC_ASSERT(_runCondition, _runMessage) \
    _Static_assert((_runCondition), _runMessage)

#define RUN_ARRAY_COUNT(_runArray) (sizeof(_runArray) / sizeof((_runArray)[0]))
#define RUN_MIN(_runA, _runB) (((_runA) < (_runB)) ? (_runA) : (_runB))
#define RUN_MAX(_runA, _runB) (((_runA) > (_runB)) ? (_runA) : (_runB))

RUN_TYPEDEF __builtin_va_list RUN_VA_LIST;
#define RUN_VA_START(_runList, _runLast) __builtin_va_start((_runList), (_runLast))
#define RUN_VA_END(_runList) __builtin_va_end(_runList)
#define RUN_VA_ARG(_runList, _runType) __builtin_va_arg((_runList), _runType)

/* The caller must ensure runValue is nonzero; CLZ(0) is undefined in C/C++. */
RUN_FORCE_INLINE RUN_UINT_32 run_compiler_count_leading_zeros(RUN_UINT_32 runValue)
{
    return (RUN_UINT_32)__builtin_clz(runValue);
}

#endif

