# SPDX-License-Identifier: Apache-2.0
# RunRTOS - board and kernel self-test component
#
# Layer: components/selftest
# Purpose: Build reusable diagnostic checks over public board APIs.
# Dependencies: F103RB board and public kernel/driver/runtime targets.
# Constraint: This component performs no board register accesses directly.

add_library(runrtos_selftest STATIC
    selftest.c
)
target_link_libraries(runrtos_selftest PUBLIC
    runrtos_bsp_f103rb
    runrtos_runtime
)
runrtos_apply_c_warnings(runrtos_selftest)
