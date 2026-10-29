# SPDX-License-Identifier: Apache-2.0
# RunRTOS - ARM GNU freestanding toolchain selection
#
# Layer: build
# Purpose: Select Cortex-M3 C/ASM compilers and object utilities.
# Constraint: Absolute toolchain roots are local defaults and may be overridden
#             through the RUNRTOS_ARM_GCC_ROOT cache variable.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(RUNRTOS_ARM_GCC_ROOT "D:/arm-none-eabi-gcc" CACHE PATH "ARM GNU toolchain root")
set(CMAKE_C_COMPILER "${RUNRTOS_ARM_GCC_ROOT}/bin/arm-none-eabi-gcc.exe")
set(CMAKE_ASM_COMPILER "${RUNRTOS_ARM_GCC_ROOT}/bin/arm-none-eabi-gcc.exe")
set(CMAKE_OBJCOPY "${RUNRTOS_ARM_GCC_ROOT}/bin/arm-none-eabi-objcopy.exe" CACHE FILEPATH "")
set(CMAKE_SIZE "${RUNRTOS_ARM_GCC_ROOT}/bin/arm-none-eabi-size.exe" CACHE FILEPATH "")

set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m3 -mthumb -mfloat-abi=soft")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=cortex-m3 -mthumb -mfloat-abi=soft")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-mcpu=cortex-m3 -mthumb -mfloat-abi=soft")

