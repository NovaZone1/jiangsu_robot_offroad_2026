#pragma once

#include "dc_motor.hpp"

// YB-DSF01 / 310 电机板级配置；2026-10-01 架空 40/60 RPM 实测。
// 完整参数和验证条件见 docs/motor_pid_20261001，160 RPM 尚未闭环验证。
namespace MotorSpeedProfiles
{
    struct Gains
    {
        float kp, ki;
    };

    constexpr float duty_limit = 0.69f;
    constexpr float maximum_target_rpm = 160.0f; // 命令限幅，不代表负载下可达到。
    constexpr float feedback_tau_s = 0.03f;
    const Gains forward[4] = {{0.001258304f, 0.019440701f},
                              {0.001273799f, 0.019587539f},
                              {0.001403675f, 0.024272447f},
                              {0.001259563f, 0.019614898f}};
    const Gains reverse[4] = {{0.001254813f, 0.019377132f},
                              {0.001455289f, 0.024360653f},
                              {0.001455132f, 0.024353350f},
                              {0.001461216f, 0.024511078f}};

    // 只能在禁能后配置。调用会清积分和目标，成功后仍需显式 Enable。
    inline bool Apply(DcMotor &motor, uint8_t port, bool backward)
    {
        if (port < 1 || port > 4 || motor.IsEnabled() || !motor.IsReady())
        {
            return false;
        }
        const auto &gains = backward ? reverse[port - 1] : forward[port - 1];
        DcMotor::SpeedPidConfig pid;
        pid.kp = gains.kp;
        pid.ki = gains.ki;
        pid.integral_limit = duty_limit;
        pid.feedback_filter_tau_s = feedback_tau_s;
        return motor.SwitchMode(DcMotor::Mode::Speed) && motor.ConfigureSpeedPid(pid) &&
               motor.DutyLimSet(duty_limit) && motor.SpeedLimSet(maximum_target_rpm) &&
               motor.SetTimeouts(250, 50);
    }
} // namespace MotorSpeedProfiles
