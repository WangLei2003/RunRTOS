# SPDX-License-Identifier: Apache-2.0
# RunRTOS - top-level build, feature generation, and shared compiler policy
#
# Layer: build
# Dependencies: CMake, selected compiler toolchain, and source-layer targets.
# Constraint: Generated configuration and binaries stay below project/build.
# All presets place generated files under project/build/<preset>. Standard-header
# and layer conformance checks run before any target is configured.

cmake_minimum_required(VERSION 3.25)

project(RunRTOS VERSION 0.1.0 LANGUAGES C ASM)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-standards.cmake)

option(RUNRTOS_HOST_TESTS "Build host-side unit tests" OFF)
option(RUNRTOS_MINIMAL "Disable optional diagnostics" OFF)
option(RUNRTOS_SHELL "Enable the RunRTOS shell" ON)
set(RUNRTOS_TICK_HZ 1000 CACHE STRING "System tick frequency")
set(RUNRTOS_PRIORITY_COUNT 32 CACHE STRING "Number of scheduler priorities")
set(RUNRTOS_MEMORY_ARENA_BYTES 8192 CACHE STRING "Runtime allocator arena size")
set(RUNRTOS_MAIN_STACK_BYTES 2048 CACHE STRING "Interrupt/main stack size")
set(RUN_CONFIG_ASSERT_ENABLED 1)

if(RUNRTOS_MINIMAL)
    set(RUNRTOS_SHELL OFF)
endif()

set(RUN_CONFIG_MINIMAL 0)
if(RUNRTOS_MINIMAL)
    set(RUN_CONFIG_MINIMAL 1)
endif()
set(RUN_CONFIG_SHELL 0)
if(RUNRTOS_SHELL)
    set(RUN_CONFIG_SHELL 1)
endif()

configure_file(
    include/runrtos/core/run_config.h.in
    ${CMAKE_BINARY_DIR}/generated/runrtos/core/run_config.h
)

add_library(runrtos_options INTERFACE)
target_include_directories(runrtos_options INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)
target_compile_features(runrtos_options INTERFACE c_std_17)
target_compile_options(runrtos_options INTERFACE
    $<$<COMPILE_LANGUAGE:C>:-ffreestanding>
    $<$<COMPILE_LANGUAGE:C>:-fno-builtin>
    $<$<COMPILE_LANGUAGE:C>:-fno-common>
    $<$<COMPILE_LANGUAGE:C>:-fno-tree-loop-distribute-patterns>
)

if(NOT RUNRTOS_HOST_TESTS)
    if(NOT RUNRTOS_BOARD STREQUAL "f103rb")
        message(FATAL_ERROR "Unsupported RunRTOS board: ${RUNRTOS_BOARD}")
    endif()
    if(NOT RUNRTOS_TARGET_MCU STREQUAL "stm32f103rb")
        message(FATAL_ERROR "Unsupported RunRTOS MCU: ${RUNRTOS_TARGET_MCU}")
    endif()
endif()

function(runrtos_apply_c_warnings target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wall>
            $<$<COMPILE_LANGUAGE:C>:-Wextra>
            $<$<COMPILE_LANGUAGE:C>:-Wpedantic>
            $<$<COMPILE_LANGUAGE:C>:-Werror>
            $<$<COMPILE_LANGUAGE:C>:-Wconversion>
            $<$<COMPILE_LANGUAGE:C>:-Wshadow>
        )
    endif()
endfunction()

add_subdirectory(src)

if(RUNRTOS_HOST_TESTS)
    enable_testing()
    add_subdirectory(tests/host)
endif()

