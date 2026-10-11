#pragma once

#include "dc_motor.hpp"

// Arbitrary gains for host-only plant/fault tests. Not hardware tuning data.
// Never include this fixture in the firmware project.
inline bool ConfigureSyntheticMotorPid(DcMotor &motor)
{
    auto pid = motor.GetSpeedPidConfig();
    pid.kp = .002f;
    pid.ki = .012f;
    pid.kd = 0;
    return motor.ConfigureSpeedPid(pid);
}
