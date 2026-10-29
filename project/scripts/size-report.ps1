# SPDX-License-Identifier: Apache-2.0
# RunRTOS - kernel-only flash and static RAM size report
#
# Layer: scripts
# Purpose: Attribute linked core/runtime/kernel/architecture sections in the map.
# Dependencies: minimal preset map/image and ARM GNU size utility.
# Constraint: User memory arenas, thread stacks, drivers, BSP, and application
#             objects are excluded. Dead-stripped archive functions are not
#             charged, matching the actual firmware image rather than archive size.
[CmdletBinding()]
param(
    [string]$ArmGccRoot = 'D:\arm-none-eabi-gcc',
    [int]$FlashBudget = 8192,
    [int]$RamBudget = 2048
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    & cmake --build --preset f103rb-minimal
    if ($LASTEXITCODE -ne 0) {
        throw 'Minimal preset build failed'
    }
}
finally {
    Pop-Location
}

$sizeExe = Join-Path $ArmGccRoot 'bin\arm-none-eabi-size.exe'
$image = Join-Path $projectRoot `
    'build\f103rb-minimal\src\applications\demo\runrtos_firmware'
$mapPath = "$image.map"
if (-not (Test-Path -LiteralPath $mapPath)) {
    throw "Linker map missing: $mapPath"
}

$flashBytes = 0
$ramBytes = 0
$vectorBytes = 0
$map = Get-Content -LiteralPath $mapPath
$mapStart = ($map | Select-String -SimpleMatch `
    'Linker script and memory map' | Select-Object -First 1).LineNumber
if ($null -eq $mapStart) {
    throw "Linker map has no section map: $mapPath"
}
$kernelSource = 'librunrtos_(core|runtime|kernel|arch_cortex_m3)\.a'
$sectionRow = '^\s*(\.[^\s]+)\s+0x[0-9A-Fa-f]+\s+0x([0-9A-Fa-f]+)\s+'

foreach ($row in $map[$mapStart..($map.Count - 1)]) {
    if ($row -notmatch $kernelSource) {
        continue
    }
    if ($row -notmatch $sectionRow) {
        continue
    }
    $section = $Matches[1]
    $bytes = [Convert]::ToInt64($Matches[2], 16)
    if (($section -match '^\.text(?:\.|$)') -or
        ($section -match '^\.rodata(?:\.|$)') -or
        ($section -match '^\.ARM\.(?:extab|exidx)(?:\.|$)')) {
        $flashBytes += $bytes
    }
    if (($section -match '^\.data(?:\.|$)') -or
        ($section -match '^\.bss(?:\.|$)') -or
        ($section -eq '.COMMON')) {
        $ramBytes += $bytes
    }
    if ($section -eq '.isr_vector') {
        $vectorBytes += $bytes
    }
}

$imageSize = & $sizeExe -A $image
if ($LASTEXITCODE -ne 0) {
    throw "size failed: $image"
}
$imageFlash = 0
foreach ($row in $imageSize) {
    if ($row -match '^\s*(\S+)\s+(\d+)\s+') {
        if ($Matches[1] -in @('.isr_vector', '.text', '.rodata',
                              '.ARM', '.ARM.extab', '.ARM.exidx', '.data')) {
            $imageFlash += [int64]$Matches[2]
        }
    }
}

Write-Host ("Kernel-only linked: Flash={0} bytes, static RAM={1} bytes" -f `
    $flashBytes, $ramBytes)
Write-Host ("Vector table (reported separately): {0} bytes" -f $vectorBytes)
Write-Host ("Full minimal image: Flash={0} bytes" -f $imageFlash)
Write-Host ("Budget: Flash<={0} bytes, static RAM<={1} bytes" -f `
    $FlashBudget, $RamBudget)
if (($flashBytes -gt $FlashBudget) -or ($ramBytes -gt $RamBudget)) {
    throw 'Kernel-only size budget exceeded'
}
