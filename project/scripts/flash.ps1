# SPDX-License-Identifier: Apache-2.0
# RunRTOS - repeatable CMSIS-DAP flash and verify workflow
#
# Layer: scripts
# Purpose: Build one preset, program only its image range, verify, and reset-run.
# Dependencies: CMake preset output and an installed OpenOCD with F1 scripts.
# Constraint: Uses 100 kHz SWD, srst_only, and reset halt; never mass-erases the
#             protected original-flash backup outside the image range.
[CmdletBinding()]
param(
    [ValidateSet('f103rb-debug', 'f103rb-release', 'f103rb-minimal')]
    [string]$Preset = 'f103rb-debug',
    [string]$OpenOcd = 'D:\OpenOCD\openocd-20260302\OpenOCD-20260302-0.12.0\bin\openocd.exe',
    [string]$Port = 'COM5',
    [int]$Baud = 115200,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$binary = Join-Path $projectRoot "build\$Preset\src\applications\demo\runrtos_firmware.bin"
$openOcdBinary = $binary.Replace('\', '/')

if (-not $SkipBuild) {
    Push-Location $projectRoot
    try {
        & cmake --build --preset $Preset
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed for preset $Preset"
        }
    }
    finally {
        Pop-Location
    }
}
if (-not (Test-Path -LiteralPath $binary)) {
    throw "Firmware image not found: $binary"
}
if (-not (Test-Path -LiteralPath $OpenOcd)) {
    throw "OpenOCD not found: $OpenOcd"
}

& $OpenOcd `
    -f 'interface/cmsis-dap.cfg' `
    -f 'target/stm32f1x.cfg' `
    -c 'adapter speed 100' `
    -c 'reset_config srst_only' `
    -c 'init' `
    -c 'reset halt' `
    -c "program {$openOcdBinary} 0x08000000 verify reset exit"
if ($LASTEXITCODE -ne 0) {
    throw "OpenOCD program/verify failed for $binary"
}

Write-Host "Flashed and verified: $binary"
Write-Host "Serial acceptance port: $Port, 115200 8N1"
Write-Host "Expected first line: RunRTOS 0.1.0 - type help for commands"
