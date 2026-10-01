param([string]$Compiler = 'g++', [string]$CCompiler = 'gcc')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$taskOutput = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskIncludes = @('-I',"$PSScriptRoot\gray_oled_mocks",
    '-I',"$PSScriptRoot\fixtures",
    '-I',"$taskRoot\FrameComponets\Bsps\Inc",'-I',"$taskRoot\FrameComponets\Mods\Inc",
    '-I',"$taskRoot\FrameComponets\Apps\Inc")
$taskObjects = @()
# Compile production C as C, just as Keil does; do not disable warning checks.
foreach ($taskName in @('bsp_gpio','bsp_oled_bus')) {
    $taskObject = Join-Path $taskOutput "gray_oled_$taskName.o"
    & $CCompiler '-std=c11' '-Wall' '-Wextra' '-Werror' @taskIncludes '-c' `
        "$taskRoot\FrameComponets\Bsps\Src\$taskName.c" '-o' $taskObject
    if ($LASTEXITCODE -ne 0) { throw "Gray OLED C build failed: $LASTEXITCODE" }
    $taskObjects += $taskObject
}
$taskExe = Join-Path $taskOutput 'gray_oled_tests.exe'
& $Compiler '-std=c++11' '-Wall' '-Wextra' '-Werror' @taskIncludes `
    "$PSScriptRoot\gray_oled_tests.cpp" `
    "$PSScriptRoot\fixtures\GrayOledTest.cpp" `
    "$taskRoot\FrameComponets\Mods\Src\gray_yahboom_8lp.cpp" `
    "$taskRoot\FrameComponets\Mods\Src\oled_factory.cpp" @taskObjects '-o' $taskExe
if ($LASTEXITCODE -ne 0) { throw "Gray OLED C++ build failed: $LASTEXITCODE" }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw "Gray OLED tests failed: $LASTEXITCODE" }
& $taskExe '--conflict'
if ($LASTEXITCODE -ne 0) { throw "Gray OLED conflict test failed: $LASTEXITCODE" }
