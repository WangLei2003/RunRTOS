# SPDX-License-Identifier: Apache-2.0
# RunRTOS - freestanding runtime target (format, memory, strings)
#
# Layer: runtime
# Purpose: Build allocator, owned strings, and bounded formatting.
# Dependencies: runrtos_core and private memory layout headers.
# Constraint: Must remain independent of kernel and hardware-specific code.

add_library(runrtos_runtime STATIC
    format/format.c
    memory/memory.c
    string/string.c
)
target_include_directories(runrtos_runtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/memory
)
target_link_libraries(runrtos_runtime PUBLIC runrtos_core)
runrtos_apply_c_warnings(runrtos_runtime)

