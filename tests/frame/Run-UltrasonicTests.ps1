param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$taskOutput = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskExe = Join-Path $taskOutput 'ultrasonic_factory_tests.exe'
$taskArgs = @('-std=c++11', '-Wall', '-Wextra', '-Werror',
    '-I', "$PSScriptRoot\mocks",
    '-I', "$taskRoot\FrameComponets\Bsps\Inc",
    '-I', "$taskRoot\FrameComponets\Mods\Inc",
    "$PSScriptRoot\ultrasonic_factory_tests.cpp",
    "$taskRoot\FrameComponets\Mods\Src\ultrasonic_factory.cpp",
    "$taskRoot\FrameComponets\Mods\Src\ultrasonic.cpp", '-o', $taskExe)
& $Compiler @taskArgs
if ($LASTEXITCODE -ne 0) { throw "Ultrasonic test build failed: $LASTEXITCODE" }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw "Ultrasonic tests failed: $LASTEXITCODE" }

$taskEchoExe = Join-Path $taskOutput 'ultrasonic_echo_tests.exe'
$taskEchoArgs = @('-std=c++11', '-Wall', '-Wextra', '-Werror',
    '-I', "$PSScriptRoot\ultrasonic_mocks",
    '-I', "$taskRoot\FrameComponets\Bsps\Inc",
    "$PSScriptRoot\ultrasonic_echo_tests.cpp",
    "$taskRoot\FrameComponets\Bsps\Src\bsp_ultrasonic_echo.c",
    "$taskRoot\FrameComponets\Bsps\Src\bsp_gpio.c", '-o', $taskEchoExe)
& $Compiler @taskEchoArgs
if ($LASTEXITCODE -ne 0) { throw "Ultrasonic BSP test build failed: $LASTEXITCODE" }
& $taskEchoExe
if ($LASTEXITCODE -ne 0) { throw "Ultrasonic BSP tests failed: $LASTEXITCODE" }
