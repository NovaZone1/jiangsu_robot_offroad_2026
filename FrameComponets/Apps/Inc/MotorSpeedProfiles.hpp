#pragma once

#include "dc_motor.hpp"

// Shared board defaults: no personal motor tuning data.
// All forward/reverse gains are zero; configure and validate on the owning robot branch.
namespace MotorSpeedProfiles
{
    struct Gains
    {
        float kp, ki;
    };

    constexpr float duty_limit = 0.69f;
    constexpr float encoder_lines = 11.0f;
    constexpr float gear_ratio = 30.0f;
    constexpr float counts_per_output_rev = encoder_lines * 4.0f * gear_ratio;
    constexpr float maximum_target_rpm = 180.0f; // 图片中的额定转速，非可达速度保证。
    constexpr float feedback_tau_s = 0.03f;
    constexpr float integral_limit = duty_limit;
    // 输入 RPM，时间秒，输出归一化 PWM，Kd=0。
    // 主分支四轮正反转均未配置，Kp/Ki/Kd 为零。
    const Gains forward[4] = {};
    const Gains reverse[4] = {};

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
        pid.kd = 0;
        pid.integral_limit = integral_limit;
        pid.feedback_filter_tau_s = feedback_tau_s;
        return motor.SwitchMode(DcMotor::Mode::Speed) && motor.ConfigureSpeedPid(pid) &&
               motor.DutyLimSet(duty_limit) && motor.SpeedLimSet(maximum_target_rpm) &&
               motor.SetTimeouts(250, 50);
    }
} // namespace MotorSpeedProfiles
