param([string]$Compiler = 'g++', [ValidateSet(1,2,3,4)][int]$TuningPort = 1)
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskArgs = @('-std=c++11','-Wall','-Wextra','-Werror','-DNOVA_MOTOR_TUNING=1',"-DNOVA_TUNING_PORT=$TuningPort",'-I',"$PSScriptRoot/mocks",'-I',"$PSScriptRoot/fixtures")
foreach ($taskLayer in @('Bsps','Mods','Apps','Sys','Algorithm')) {
    $taskArgs += @('-I',"$taskRoot/FrameComponets/$taskLayer/Inc")
}
$taskArgs += "$PSScriptRoot/nova_m1_tests.cpp"
$taskArgs += "$PSScriptRoot/fixtures/MotorContinuousTest.cpp"
foreach ($taskSource in @('Bsps/Src/bsp_dwt.c','Algorithm/Src/pid.cpp','Algorithm/Src/std_math.cpp',
    'Mods/Src/dc_motor.cpp','Mods/Src/gray_sensor.cpp','Mods/Src/oled_factory.cpp',
    'Mods/Src/bluetooth.cpp','Mods/Src/nova_protocol.cpp',
    'Sys/Src/Application.cpp','Sys/Src/System.cpp','Sys/Src/StateCore.cpp','Sys/Src/Action.cpp','Sys/Src/Monitor.cpp',
    'Apps/Src/Track.cpp','Apps/Src/SpeedMixer.cpp','Apps/Src/MotorSpeedDisplay.cpp',
    'Apps/Src/LineFollowApp.cpp','Apps/Src/MotorProtection.cpp','Apps/Src/NovaLink.cpp')) {
    $taskArgs += "$taskRoot/FrameComponets/$taskSource"
}
New-Item -ItemType Directory -Path "$PSScriptRoot/out" -Force | Out-Null
$taskExe = "$PSScriptRoot/out/nova_m${TuningPort}_tests.exe"
& $Compiler @taskArgs '-o' $taskExe
if ($LASTEXITCODE -ne 0) { throw 'Nova binding test build failed' }
foreach ($scenario in @('normal','pid','watchdog','gap','feedback','no_motion','direction','overspeed','uart','ack','duration','other_enabled',
    'monitor_normal','monitor_no_motion','monitor_stop_wait')) {
    & $taskExe $scenario
    if ($LASTEXITCODE -ne 0) { throw "M$TuningPort test failed: $scenario" }
}
