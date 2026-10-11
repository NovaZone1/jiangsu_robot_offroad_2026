param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskArgs = @('-std=c++11','-Wall','-Wextra','-Werror','-I',"$PSScriptRoot/mocks")
foreach ($taskLayer in @('Bsps','Mods','Apps','Sys','Algorithm')) {
    $taskArgs += @('-I',"$taskRoot/FrameComponets/$taskLayer/Inc")
}
$taskArgs += "$PSScriptRoot/nova_link_tests.cpp"
foreach ($taskSource in @('Bsps/Src/bsp_dwt.c','Algorithm/Src/pid.cpp','Algorithm/Src/std_math.cpp',
    'Mods/Src/dc_motor.cpp','Mods/Src/gray_sensor.cpp','Mods/Src/oled_factory.cpp',
    'Mods/Src/bluetooth.cpp','Mods/Src/nova_protocol.cpp',
    'Sys/Src/Application.cpp','Sys/Src/System.cpp','Sys/Src/StateCore.cpp','Sys/Src/Action.cpp','Sys/Src/Monitor.cpp',
    'Apps/Src/Track.cpp','Apps/Src/SpeedMixer.cpp','Apps/Src/MotorSpeedDisplay.cpp',
    'Apps/Src/LineFollowApp.cpp','Apps/Src/MotorProtection.cpp','Apps/Src/NovaLink.cpp')) {
    $taskArgs += "$taskRoot/FrameComponets/$taskSource"
}
New-Item -ItemType Directory -Path "$PSScriptRoot/out" -Force | Out-Null
$taskExe = "$PSScriptRoot/out/nova_link_tests.exe"
& $Compiler @taskArgs '-o' $taskExe
if ($LASTEXITCODE -ne 0) { throw 'Nova binding test build failed' }
& $taskExe
if ($LASTEXITCODE -ne 0) { throw 'Nova binding tests failed' }
