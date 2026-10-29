# SPDX-License-Identifier: Apache-2.0
# RunRTOS - reproducible software acceptance matrix
#
# Layer: scripts
# Purpose: Build and test host/debug/release/minimal, reject unresolved symbols,
#          audit allowed GCC helpers, and enforce the kernel size thresholds.
# Dependencies: CMake presets, CTest, ARM GNU nm, and size-report.ps1.
# Constraint: Any warning, failed test, unresolved symbol, or disallowed helper is
#             a hard failure. Hardware flashing is intentionally a separate step.
[CmdletBinding()]
param(
    [string]$ArmGccRoot = 'D:\arm-none-eabi-gcc'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$presets = @('host-tests', 'f103rb-debug', 'f103rb-release', 'f103rb-minimal')

Push-Location $projectRoot
try {
    foreach ($preset in $presets) {
        & cmake --preset $preset
        if ($LASTEXITCODE -ne 0) {
            throw "Configure failed: $preset"
        }
        & cmake --build --preset $preset
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed: $preset"
        }
    }

    & ctest --preset host-tests --timeout 20 --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "Host tests failed"
    }
}
finally {
    Pop-Location
}

$nm = Join-Path $ArmGccRoot 'bin\arm-none-eabi-nm.exe'
if (-not (Test-Path -LiteralPath $nm)) {
    throw "arm-none-eabi-nm not found: $nm"
}
$releaseImage = Join-Path $projectRoot 'build\f103rb-release\src\applications\demo\runrtos_firmware'
$undefined = & $nm -u $releaseImage
if ($LASTEXITCODE -ne 0) {
    throw "nm failed: $releaseImage"
}
if ($undefined.Count -ne 0) {
    $undefined | Write-Host
    throw "Release image contains unresolved symbols"
}

$allowedHelpers = @(
    '__aeabi_idiv0',
    '__aeabi_ldiv0',
    '__aeabi_uldivmod',
    '__udivmoddi4'
)
$linkedHelpers = & $nm $releaseImage |
    Select-String -Pattern '^[0-9A-Fa-f]+\s+[TtWw]\s+(__[A-Za-z0-9_]+)$' |
    ForEach-Object {
        $_.Matches[0].Groups[1].Value
    } |
    Sort-Object -Unique
foreach ($helper in $linkedHelpers) {
    if ($allowedHelpers -notcontains $helper) {
        throw "Disallowed GCC helper linked: $helper"
    }
}

& (Join-Path $PSScriptRoot 'size-report.ps1') -ArmGccRoot $ArmGccRoot
if ($LASTEXITCODE -ne 0) {
    throw "Kernel size report exceeded its budget"
}
Write-Host 'RunRTOS software acceptance matrix passed.'
