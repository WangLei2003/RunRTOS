# SPDX-License-Identifier: Apache-2.0
# RunRTOS - host test executable and CTest registration
#
# Layer: tests/host
# Purpose: Build and register runtime smoke tests with CTest.
# Dependencies: Runtime public APIs, host C compiler, and CTest.
# Constraint: Hardware register access is excluded so tests remain reproducible.

add_executable(runrtos_host_tests
    test_main.c
)
target_link_libraries(runrtos_host_tests PRIVATE
    runrtos_core
    runrtos_runtime
)
runrtos_apply_c_warnings(runrtos_host_tests)

add_test(NAME runrtos_host_tests COMMAND runrtos_host_tests)

