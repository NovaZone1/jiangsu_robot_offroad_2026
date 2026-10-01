param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$taskOutput = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskArgs = @('-std=c++11','-Wall','-Wextra','-Werror',
    '-I',"$PSScriptRoot\gray_8lp_mocks",'-I',"$PSScriptRoot\mocks",
    '-I',"$taskRoot\FrameComponets\Bsps\Inc",'-I',"$taskRoot\FrameComponets\Mods\Inc",
    '-I',"$taskRoot\FrameComponets\Apps\Inc", "$PSScriptRoot\gray_8lp_tests.cpp")
foreach ($taskSource in @('Bsps/Src/bsp_gpio.c','Mods/Src/gray_sensor.cpp',
    'Mods/Src/gray_yahboom_8lp.cpp','Apps/Src/gray_8lp_example.cpp')) {
    $taskArgs += "$taskRoot\FrameComponets\$taskSource"
}
$taskExe = Join-Path $taskOutput 'gray_8lp_tests.exe'
& $Compiler @taskArgs '-o' $taskExe
if ($LASTEXITCODE -ne 0) { throw "Gray 8-LP test build failed: $LASTEXITCODE" }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw "Gray 8-LP tests failed: $LASTEXITCODE" }
