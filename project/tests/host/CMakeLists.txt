# SPDX-License-Identifier: Apache-2.0
# RunRTOS - host test executable and CTest registration
#
# Layer: tests/host
# Purpose: Build and register runtime/kernel/sync/driver tests with CTest.
# Dependencies: Public APIs, production kernel, Windows Fiber test port, and CTest.
# Constraint: Hardware register access is excluded so tests remain reproducible.

add_executable(runrtos_host_tests
    test_main.c
    support/host_arch.c
    test_runtime.c
    test_kernel.c
    test_sync.c
    test_driver.c
)
target_include_directories(runrtos_host_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..//src/kernel/internal
)
target_link_libraries(runrtos_host_tests PRIVATE
    runrtos_core
    runrtos_runtime
    runrtos_kernel
    runrtos_drivers
)
runrtos_apply_c_warnings(runrtos_host_tests)

add_test(NAME runrtos_host_tests COMMAND runrtos_host_tests)

