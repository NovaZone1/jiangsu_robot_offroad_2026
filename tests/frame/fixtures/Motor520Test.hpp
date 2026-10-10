#pragma once

#include "dc_motor.hpp"

// Historical bench sequence, host regression only; never compiled into firmware.
class Motor520Test
{
public:
    enum class Phase
    {
        Idle,
        Waiting,
        ForwardSlow,
        ForwardFast,
        Coasting,
        ReverseSlow,
        ReverseFast,
        Finished,
        Failed
    };
    enum class Failure
    {
        None,
        MotorNotReady,
        ControlGap,
        FeedbackLost,
        MotorFault,
        WrongDirection,
        NoMotion,
        Overspeed,
        CoastTimeout,
        InvalidPorts
    };

    struct Status
    {
        Phase phase = Phase::Idle;
        Failure failure = Failure::None;
        uint8_t failed_port = 0; // 1～4；0 表示整车调度问题。
        uint8_t port_mask = 0; // bit0～bit3：本次参与测试的 M1～M4。
        float target_rpm = 0;
        uint32_t phase_elapsed_ms = 0;
    };

    static constexpr float slow_rpm = 60.0f;
    static constexpr float fast_rpm = 120.0f;
    static constexpr uint32_t startup_ms = 2000;
    static constexpr uint32_t stage_ms = 5000;
    static constexpr uint32_t coast_min_ms = 1000;
    static constexpr uint32_t coast_timeout_ms = 3000;

    Motor520Test(DcMotor &m1, DcMotor &m2, DcMotor &m3, DcMotor &m4, uint8_t port_mask);
    bool Start(); // 每次上电只能启动一次；失败和完成后不自动重试。
    void Update(); // 每 1 ms 调用；此入口负责四轮 Control，外部不能再写目标。
    const Status &GetStatus() const;

private:
    void StopAll();
    void Fail(Failure failure, uint8_t port = 0);
    bool Begin(Phase phase, uint32_t now, bool apply_profile);
    bool CheckHealth(uint32_t now);
    bool IsMovingPhase() const;
    bool IsSelected(uint8_t index) const;

    DcMotor *motors_[4];
    const uint8_t port_mask_;
    Status status_;
    uint32_t phase_tick_ = 0;
    uint32_t last_tick_ = 0;
    uint32_t still_tick_ = 0;
    bool still_ = false;
    uint32_t last_motion_tick_[4] = {};
    uint32_t sample_count_[4] = {};
    uint8_t wrong_samples_[4] = {};
};
