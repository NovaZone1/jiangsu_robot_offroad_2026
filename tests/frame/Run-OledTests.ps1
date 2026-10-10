param([string]$Compiler = 'g++', [string]$CCompiler = '')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$taskOutput = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
if (-not $CCompiler) { $CCompiler = $Compiler -replace 'g\+\+(\.exe)?$', 'gcc$1' }
$taskCIncludes = @('-I', "$PSScriptRoot\oled_mocks", '-I', "$taskRoot\FrameComponets\Bsps\Inc")
$taskObjects = @()
# Compile BSP .c files as C, matching the firmware compiler.
foreach ($taskName in @('bsp_oled_bus', 'bsp_gpio')) {
    $taskObject = Join-Path $taskOutput "range_oled_$taskName.o"
    & $CCompiler '-std=c11' '-Wall' '-Wextra' '-Werror' @taskCIncludes '-c' `
        "$taskRoot\FrameComponets\Bsps\Src\$taskName.c" '-o' $taskObject
    if ($LASTEXITCODE -ne 0) { throw "OLED C build failed: $LASTEXITCODE" }
    $taskObjects += $taskObject
}
$taskExe = Join-Path $taskOutput 'oled_factory_tests.exe'
$taskArgs = @('-std=c++11', '-Wall', '-Wextra', '-Werror',
    '-I', "$PSScriptRoot\oled_mocks",
    '-I', "$PSScriptRoot\fixtures",
    '-I', "$taskRoot\FrameComponets\Bsps\Inc",
    '-I', "$taskRoot\FrameComponets\Mods\Inc",
    '-I', "$taskRoot\FrameComponets\Apps\Inc",
    '-I', "$taskRoot\FrameComponets\Sys\Inc",
    "$PSScriptRoot\oled_factory_tests.cpp", "$PSScriptRoot\fixtures\RangeDisplayApp.cpp")
foreach ($taskSource in @('Mods/Src/oled_factory.cpp', 'Mods/Src/ultrasonic_factory.cpp', 'Mods/Src/ultrasonic.cpp',
    'Sys/Src/Application.cpp')) {
    $taskArgs += Join-Path "$taskRoot\FrameComponets" $taskSource
}
& $Compiler @taskArgs @taskObjects '-o' $taskExe
if ($LASTEXITCODE -ne 0) { throw "OLED test build failed: $LASTEXITCODE" }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw "OLED tests failed: $LASTEXITCODE" }
