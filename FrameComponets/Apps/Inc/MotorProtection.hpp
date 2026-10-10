#pragma once

#include "dc_motor.hpp"

// Four-wheel protection only: never generates a movement command or enables motors.
// Call BeforeControl before application output, AfterControl after RPM acquisition.
class MotorProtection
{
public:
    enum class Fault
    {
        None,
        NotReady,
        ControlGap,
        FeedbackLost,
        MotorFault,
        WrongDirection,
        NoMotion,
        Overspeed,
        CoastTimeout
    };
    enum class Coast
    {
        None,
        Waiting,
        Ready
    };

    struct Status
    {
        Fault fault = Fault::None;
        uint8_t failed_port = 0; // 1..4, zero for a global timing/coast fault.
        Coast coast = Coast::None;
    };

    MotorProtection(DcMotor &m1, DcMotor &m2, DcMotor &m3, DcMotor &m4);
    bool Init(); // All four motors ready, disabled and free of driver faults.
    bool BeforeControl();
    bool AfterControl();
    bool RequestCoastStop(); // Disable all, wait for standstill; never auto-restart.
    bool CanResumeAfterCoast() const;
    bool ReleaseCoastStop(); // Explicit release after standstill; motors stay disabled.
    bool IsCoasting() const;
    const Status &GetStatus() const;

private:
    bool Fail(Fault fault, uint8_t port = 0);
    void StopAll();
    DcMotor *motors_[4];
    Status status_;
    uint32_t last_tick_ = 0;
    uint32_t command_since_[4] = {};
    uint32_t last_motion_[4] = {};
    uint32_t samples_[4] = {};
    int8_t direction_[4] = {};
    uint8_t wrong_samples_[4] = {};
    uint32_t coast_since_ = 0;
    uint32_t still_since_ = 0;
    bool still_ = false;
    bool active_last_tick_ = false;
    bool initialized_ = false;
};
