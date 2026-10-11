param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskArgs = @('-std=c++11','-Wall','-Wextra','-Werror','-I',"$PSScriptRoot/mocks")
foreach ($taskLayer in @('Bsps','Mods','Apps','Sys','Algorithm')) {
    $taskArgs += @('-I',"$taskRoot/FrameComponets/$taskLayer/Inc")
}
$taskArgs += "$PSScriptRoot/m1_continuous_tests.cpp"
foreach ($taskSource in @('Bsps/Src/bsp_dwt.c','Algorithm/Src/pid.cpp','Algorithm/Src/std_math.cpp',
    'Mods/Src/dc_motor.cpp','Apps/Src/MotorProtection.cpp','Apps/Src/MotorContinuousTest.cpp')) {
    $taskArgs += "$taskRoot/FrameComponets/$taskSource"
}
New-Item -ItemType Directory -Path "$PSScriptRoot/out" -Force | Out-Null
$taskExe = "$PSScriptRoot/out/m1_continuous_tests.exe"
& $Compiler @taskArgs '-o' $taskExe
if ($LASTEXITCODE -ne 0) { throw 'M1 continuous test build failed' }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw 'M1 continuous tests failed' }
