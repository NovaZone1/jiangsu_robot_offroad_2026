#pragma once

#include "MotorProtection.hpp"

#ifndef MOTOR_CONTINUOUS_TEST
#define MOTOR_CONTINUOUS_TEST 1
#endif

// Temporary local bench mode. Takes exclusive ownership before Nova/competition dispatch.
class MotorContinuousTest
{
public:
    static constexpr bool Enabled = MOTOR_CONTINUOUS_TEST != 0;
    static constexpr float TargetRpm = 150.0f;
    static constexpr float DutyLimit = 0.5f;
    static constexpr uint8_t SelectedPort =
        3; // M3 right front; physical port numbering stays 1..4.
    static constexpr uint8_t SelectedIndex = SelectedPort - 1;
    static_assert(SelectedPort >= 1 && SelectedPort <= 4, "Invalid test motor port");
    enum class Phase
    {
        Disarmed,
        Waiting,
        Running,
        Fault
    };
    enum class Failure
    {
        None,
        Initialization,
        Protection,
        Start,
        UnexpectedDisable,
        ExternalStop
    };

    struct Status
    {
        Phase phase = Phase::Disarmed;
        Failure failure = Failure::None;
        uint8_t port = SelectedPort;
        // Last enabled control sample is retained after a protection stop.
        float target_rpm = 0, measured_rpm = 0, duty = 0;
        uint32_t sample_tick = 0;
    };

    MotorContinuousTest(DcMotor &m1, DcMotor &m2, DcMotor &m3, DcMotor &m4,
                        MotorProtection &protection);
    bool Init(); // Once after board/protection initialization; never clears an existing fault.
    bool Run(); // Same 1 ms task; includes the unchanged Before/AfterControl guards.
    void Stop(); // Latched stop; restart requires MCU reset.
    const Status &GetStatus() const;

private:
    bool Fail(Failure failure);
    DcMotor *motors_[4];
    MotorProtection &protection_;
    Status status_;
    uint32_t start_tick_ = 0;
};

extern MotorContinuousTest MotorBench;
