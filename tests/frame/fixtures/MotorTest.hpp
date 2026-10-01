#pragma once

#include "dc_motor.hpp"

// Historical four-wheel bench sequence, retained only for host regression tests.
class MotorTest
{
public:
    enum class Phase
    {
        Idle,
        WaitingForFeedback,
        Forward,
        Reverse,
        Finished,
        Failed
    };

    enum class Failure
    {
        None,
        Setup,
        FeedbackUnavailable,
        Enable,
        MotorFault,
        ControlTimeout,
        WrongDirection,
        NoMotion
    };

    struct Config
    {
        float target_rpm = 60;
        float speed_limit_rpm = 80;
        float duty_limit = 0.15f; // PID 控制量；实际 PWM 还需加驱动死区补偿。
        float kp = 0.0008f;
        float ki = 0.005f;
        float kd = 0;
        float integral_limit = 0.1f;
    };

    struct Status
    {
        Phase phase = Phase::Idle;
        Failure failure = Failure::None;
        uint8_t failed_port = 0; // 1..4，0 表示整体配置错误
        uint32_t elapsed_ms = 0;
        float target_rpm = 0;
    };

    MotorTest(DcMotor &first, DcMotor &second, DcMotor &third, DcMotor &fourth);
    bool Start();
    bool Start(const Config &config);
    void Update();
    const Status &GetStatus() const;

private:
    void StopAll();
    void Fail(Failure reason, uint8_t port);
    void BeginPhase(Phase phase, uint32_t now);
    bool CheckMotion(uint32_t now, uint8_t index);

    DcMotor *motors_[4];
    Config config_;
    Status status_;
    uint32_t phase_tick_ = 0;
    uint32_t last_update_tick_ = 0;
    uint32_t motion_tick_[4] = {};
    uint32_t sample_count_[4] = {};
    uint8_t wrong_direction_count_[4] = {};
};
