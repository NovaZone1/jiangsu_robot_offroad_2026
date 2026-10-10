param([string]$Compiler = 'g++', [string]$CCompiler = '')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$taskOutput = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
if (-not $CCompiler) { $CCompiler = $Compiler -replace 'g\+\+(\.exe)?$', 'gcc$1' }
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
$taskCIncludes = @('-I', "$PSScriptRoot\ultrasonic_mocks", '-I', "$taskRoot\FrameComponets\Bsps\Inc")
$taskObjects = @()
foreach ($taskName in @('bsp_ultrasonic_echo', 'bsp_gpio')) {
    $taskObject = Join-Path $taskOutput "ultrasonic_$taskName.o"
    & $CCompiler '-std=c11' '-Wall' '-Wextra' '-Werror' @taskCIncludes '-c' `
        "$taskRoot\FrameComponets\Bsps\Src\$taskName.c" '-o' $taskObject
    if ($LASTEXITCODE -ne 0) { throw "Ultrasonic C build failed: $LASTEXITCODE" }
    $taskObjects += $taskObject
}
$taskEchoArgs = @('-std=c++11', '-Wall', '-Wextra', '-Werror',
    '-I', "$PSScriptRoot\ultrasonic_mocks",
    '-I', "$taskRoot\FrameComponets\Bsps\Inc",
    "$PSScriptRoot\ultrasonic_echo_tests.cpp")
& $Compiler @taskEchoArgs @taskObjects '-o' $taskEchoExe
if ($LASTEXITCODE -ne 0) { throw "Ultrasonic BSP test build failed: $LASTEXITCODE" }
& $taskEchoExe
if ($LASTEXITCODE -ne 0) { throw "Ultrasonic BSP tests failed: $LASTEXITCODE" }
