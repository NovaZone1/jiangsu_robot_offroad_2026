#pragma once

#include "dc_motor.hpp"

// YB-DSF01 / 520、30:1；四轮正转参数来自 2026-10-11 Nova 100 RPM 台架复测。
// 旧 310 电机实测参数仅保留在 docs/motor_pid_20261001，不适用于本电机。
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
    // 四轮正转使用各自复测参数；反转仍保留未调参初值。
    const Gains forward[4] = {{0.000957f, 0.009191f},
                              {0.001329f, 0.010857f},
                              {0.001176f, 0.013926f},
                              {0.001442f, 0.010392f}};
    const Gains reverse[4] = {
        {0.0012f, 0.008f}, {0.0012f, 0.008f}, {0.0012f, 0.008f}, {0.0012f, 0.008f}};

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
        pid.integral_limit = integral_limit;
        pid.feedback_filter_tau_s = feedback_tau_s;
        return motor.SwitchMode(DcMotor::Mode::Speed) && motor.ConfigureSpeedPid(pid) &&
               motor.DutyLimSet(duty_limit) && motor.SpeedLimSet(maximum_target_rpm) &&
               motor.SetTimeouts(250, 50);
    }
} // namespace MotorSpeedProfiles
