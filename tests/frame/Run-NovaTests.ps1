param([string]$Compiler = 'g++', [string]$CCompiler = '')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskOutput = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
if (-not $CCompiler) { $CCompiler = $Compiler -replace 'g\+\+(\.exe)?$', 'gcc$1' }
$taskIncludes = @('-I', "$PSScriptRoot/uart_mocks", '-I', "$taskRoot/FrameComponets/Bsps/Inc",
    '-I', "$taskRoot/FrameComponets/Mods/Inc", '-I', "$taskRoot/FrameComponets/Sys/Inc")
$taskObjects = @()
foreach ($taskName in @('bsp_uart1_dma','bsp_gpio')) {
    $taskObject = Join-Path $taskOutput "nova_$taskName.o"
    & $CCompiler '-std=c11' '-Wall' '-Wextra' '-Werror' @taskIncludes '-c' `
        "$taskRoot/FrameComponets/Bsps/Src/$taskName.c" '-o' $taskObject
    if ($LASTEXITCODE -ne 0) { throw 'UART DMA C build failed' }
    $taskObjects += $taskObject
}
$taskExe = Join-Path $taskOutput 'uart_dma_tests.exe'
& $Compiler '-std=c++11' '-Wall' '-Wextra' '-Werror' @taskIncludes `
    "$PSScriptRoot/uart_dma_tests.cpp" "$taskRoot/FrameComponets/Mods/Src/bluetooth.cpp" @taskObjects '-o' $taskExe
if ($LASTEXITCODE -ne 0) { throw 'UART test build failed' }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw 'UART DMA tests failed' }
$taskExe = Join-Path $taskOutput 'nova_protocol_tests.exe'
& $Compiler '-std=c++11' '-Wall' '-Wextra' '-Werror' @taskIncludes `
    "$PSScriptRoot/nova_protocol_tests.cpp" "$taskRoot/FrameComponets/Mods/Src/nova_protocol.cpp" '-o' $taskExe
if ($LASTEXITCODE -ne 0) { throw 'Nova protocol build failed' }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw 'Nova protocol tests failed' }
& (Join-Path $PSScriptRoot 'Run-NovaLinkTests.ps1') -Compiler $Compiler

& (Join-Path $PSScriptRoot 'Run-NovaM1Tests.ps1') -Compiler $Compiler

& (Join-Path $PSScriptRoot 'Run-NovaM1Tests.ps1') -Compiler $Compiler -TuningPort 3

& (Join-Path $PSScriptRoot 'Run-NovaM1Tests.ps1') -Compiler $Compiler -TuningPort 2

& (Join-Path $PSScriptRoot 'Run-NovaM1Tests.ps1') -Compiler $Compiler -TuningPort 4
