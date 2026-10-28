param([string]$Compiler = 'g++')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskArgs = @('-std=c++11','-Wall','-Wextra','-Werror')
$taskArgs += '-DLINE_FOLLOW_ULTRASONIC_START=1'
foreach ($taskInclude in @("$PSScriptRoot/mocks", "$taskRoot/FrameComponets/Bsps/Inc", "$taskRoot/FrameComponets/Mods/Inc", "$taskRoot/FrameComponets/Apps/Inc", "$taskRoot/FrameComponets/Sys/Inc", "$taskRoot/FrameComponets/Algorithm/Inc")) {
    $taskArgs += @('-I', $taskInclude)
}
$taskArgs += "$PSScriptRoot/legacy_line_run_tests.cpp"
foreach ($taskSource in @('Bsps/Src/bsp_dwt.c','Algorithm/Src/pid.cpp','Algorithm/Src/std_math.cpp','Mods/Src/dc_motor.cpp','Mods/Src/gray_sensor.cpp','Mods/Src/ultrasonic.cpp','Mods/Src/oled_factory.cpp','Sys/Src/Application.cpp','Sys/Src/System.cpp','Sys/Src/StateCore.cpp','Sys/Src/Action.cpp','Sys/Src/Monitor.cpp','Apps/Src/Track.cpp','Apps/Src/SpeedMixer.cpp','Apps/Src/MotorSpeedDisplay.cpp','Apps/Src/LineFollowApp.cpp','Apps/Src/MotorProtection.cpp','Apps/Src/LegacyLineRun.cpp')) {
    $taskArgs += "$taskRoot/FrameComponets/$taskSource"
}
New-Item -ItemType Directory -Path "$PSScriptRoot/out" -Force | Out-Null
$taskExe = "$PSScriptRoot/out/legacy_line_run_tests.exe"
& $Compiler @taskArgs '-o' $taskExe
if ($LASTEXITCODE -ne 0) { throw 'Legacy line test build failed' }
$taskScenarios = @('gate','normal','wrap','boundary','range_invalid','cached','no_line','wide_line','separated_line','early_stop','external_enable','reset_hold','gray_stale','loss','gap')
$taskScenarios += @('one_second','near70','target_bound','dashed')
$taskScenarios += @('course_planner','finish_gate','course_three_laps','course_three_laps_left','no_time_limit')
foreach ($taskPort in 1,2,3,4) {
    foreach ($taskFault in 'disabled','feedback','driver','overspeed','direction','stall','saturation') {
        $taskScenarios += "${taskFault}_$taskPort"
    }
}
foreach ($taskScenario in $taskScenarios) {
    & $taskExe $taskScenario
    if ($LASTEXITCODE -ne 0) { throw "Legacy line test failed: $taskScenario" }
}
