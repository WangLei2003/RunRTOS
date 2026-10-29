# SPDX-License-Identifier: Apache-2.0
# RunRTOS - core target (status names and fatal assertion support)
#
# Layer: core
# Purpose: Build status-name and assertion support for every upper layer.
# Dependencies: Generated build options only.
# Constraint: Must not depend on runtime, kernel, drivers, or platform headers.

add_library(runrtos_core STATIC
    assert/assert.c
    types/status.c
)
target_link_libraries(runrtos_core PUBLIC runrtos_options)
runrtos_apply_c_warnings(runrtos_core)

