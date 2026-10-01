param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$taskOutput = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskExe = Join-Path $taskOutput 'oled_factory_tests.exe'
$taskArgs = @('-std=c++11', '-Wall', '-Wextra', '-Werror',
    '-I', "$PSScriptRoot\oled_mocks",
    '-I', "$PSScriptRoot\fixtures",
    '-I', "$taskRoot\FrameComponets\Bsps\Inc",
    '-I', "$taskRoot\FrameComponets\Mods\Inc",
    '-I', "$taskRoot\FrameComponets\Apps\Inc",
    '-I', "$taskRoot\FrameComponets\Sys\Inc",
    "$PSScriptRoot\oled_factory_tests.cpp", "$PSScriptRoot\fixtures\RangeDisplayApp.cpp")
foreach ($taskSource in @('Bsps/Src/bsp_oled_bus.c', 'Bsps/Src/bsp_gpio.c',
    'Mods/Src/oled_factory.cpp', 'Mods/Src/ultrasonic_factory.cpp', 'Mods/Src/ultrasonic.cpp',
    'Sys/Src/Application.cpp')) {
    $taskArgs += Join-Path "$taskRoot\FrameComponets" $taskSource
}
& $Compiler @taskArgs '-o' $taskExe
if ($LASTEXITCODE -ne 0) { throw "OLED test build failed: $LASTEXITCODE" }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw "OLED tests failed: $LASTEXITCODE" }
