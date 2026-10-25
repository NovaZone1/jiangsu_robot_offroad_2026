#include "MotorProtection.hpp"
#include "stm32f1xx_hal.h"
#include <math.h>

namespace
{
    constexpr uint32_t control_gap_ms = 50;
    constexpr uint32_t direction_grace_ms = 500;
    constexpr uint32_t no_motion_ms = 1500;
    constexpr uint32_t standstill_ms = 100;
    constexpr uint32_t coast_min_ms = 1000;
    constexpr uint32_t coast_timeout_ms = 3000;
    constexpr float motion_rpm = 3.0f;
    constexpr float standstill_rpm = 6.0f;
    constexpr float overspeed_rpm = 400.0f;
} // namespace

MotorProtection::MotorProtection(DcMotor &m1, DcMotor &m2, DcMotor &m3, DcMotor &m4)
    : motors_{&m1, &m2, &m3, &m4}
{
}

void MotorProtection::StopAll()
{
    for (auto motor : motors_)
    {
        motor->Disable();
    }
}

bool MotorProtection::Fail(Fault fault, uint8_t port)
{
    StopAll();
    if (status_.fault == Fault::None)
    {
        status_.fault = fault;
        status_.failed_port = port;
    }
    return false;
}

bool MotorProtection::Init()
{
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (!motors_[i]->IsReady() || motors_[i]->IsEnabled() ||
            motors_[i]->GetFault() != DcMotor::Fault::None)
        {
            return Fail(Fault::NotReady, i + 1U);
        }
        for (uint8_t j = 0; j < i; ++j)
        {
            if (motors_[i] == motors_[j])
            {
                return Fail(Fault::NotReady, i + 1U);
            }
        }
    }
    status_ = {};
    for (uint8_t i = 0; i < 4; ++i)
    {
        direction_[i] = 0;
        wrong_samples_[i] = 0;
        samples_[i] = motors_[i]->GetMeasure().sample_count;
    }
    last_tick_ = HAL_GetTick();
    active_last_tick_ = still_ = false;
    initialized_ = true;
    return true;
}

bool MotorProtection::BeforeControl()
{
    if (!initialized_ || status_.fault != Fault::None)
    {
        StopAll();
        return false;
    }
    const uint32_t now = HAL_GetTick();
    bool active = IsCoasting();
    for (auto motor : motors_)
    {
        active = active || motor->IsEnabled();
    }
    if ((active || active_last_tick_) && (uint32_t)(now - last_tick_) >= control_gap_ms)
    {
        return Fail(Fault::ControlGap);
    }
    active_last_tick_ = active;
    last_tick_ = now;
    if (IsCoasting())
    {
        StopAll();
    }
    return true;
}

bool MotorProtection::AfterControl()
{
    if (!initialized_ || status_.fault != Fault::None)
    {
        StopAll();
        return false;
    }
    const uint32_t now = HAL_GetTick();
    bool stopped = true;
    for (uint8_t i = 0; i < 4; ++i)
    {
        const auto &motor = *motors_[i];
        if (motor.GetFault() != DcMotor::Fault::None)
        {
            return Fail(motor.GetFault() == DcMotor::Fault::FeedbackLost ? Fault::FeedbackLost
                                                                         : Fault::MotorFault,
                        i + 1U);
        }
        if ((motor.IsEnabled() || IsCoasting()) && !motor.IsOnline())
        {
            return Fail(Fault::FeedbackLost, i + 1U);
        }
        const auto &measure = motor.GetMeasure();
        if (motor.IsOnline() && fabsf(measure.speed_rpm) > overspeed_rpm)
        {
            return Fail(Fault::Overspeed, i + 1U);
        }
        stopped = stopped && motor.IsOnline() && fabsf(measure.speed_rpm) < standstill_rpm;
        const float command =
            motor.GetMode() == DcMotor::Mode::Speed ? motor.GetTargetSpeed() : motor.GetDuty();
        const int8_t direction =
            !motor.IsEnabled() || IsCoasting() || command == 0 ? 0 : (command > 0 ? 1 : -1);
        if (direction != direction_[i])
        {
            direction_[i] = direction;
            command_since_[i] = last_motion_[i] = now;
            wrong_samples_[i] = 0;
        }
        if (direction == 0)
        {
            wrong_samples_[i] = 0;
            samples_[i] = measure.sample_count;
            continue;
        }
        if (samples_[i] == measure.sample_count)
        {
            continue;
        }
        samples_[i] = measure.sample_count;
        if (fabsf(measure.speed_rpm) >= motion_rpm)
        {
            last_motion_[i] = now;
        }
        if ((uint32_t)(now - last_motion_[i]) >= no_motion_ms)
        {
            return Fail(Fault::NoMotion, i + 1U);
        }
        if ((uint32_t)(now - command_since_[i]) >= direction_grace_ms &&
            measure.speed_rpm * direction < -motion_rpm)
        {
            if (++wrong_samples_[i] >= 3)
            {
                return Fail(Fault::WrongDirection, i + 1U);
            }
        }
        else
        {
            wrong_samples_[i] = 0;
        }
    }
    if (status_.coast == Coast::Ready && !stopped)
    {
        status_.coast = Coast::Waiting;
        // A completed stop has no live deadline. Manual movement starts one new bounded
        // standstill check; continued movement while Waiting must never refresh this timer.
        coast_since_ = now;
        still_ = false;
    }
    if (status_.coast == Coast::Waiting)
    {
        if (stopped && !still_)
        {
            still_ = true;
            still_since_ = now;
        }
        else if (!stopped)
        {
            still_ = false;
        }
        if ((uint32_t)(now - coast_since_) >= coast_min_ms && still_ &&
            (uint32_t)(now - still_since_) >= standstill_ms)
        {
            status_.coast = Coast::Ready;
        }
        else if ((uint32_t)(now - coast_since_) >= coast_timeout_ms)
        {
            return Fail(Fault::CoastTimeout);
        }
    }
    return true;
}

bool MotorProtection::RequestCoastStop()
{
    if (!initialized_ || status_.fault != Fault::None || IsCoasting())
    {
        return false;
    }
    StopAll();
    coast_since_ = HAL_GetTick();
    still_ = false;
    status_.coast = Coast::Waiting;
    return true;
}

bool MotorProtection::CanResumeAfterCoast() const
{
    return status_.fault == Fault::None && status_.coast == Coast::Ready;
}

bool MotorProtection::ReleaseCoastStop()
{
    if (!CanResumeAfterCoast())
    {
        return false;
    }
    StopAll();
    status_.coast = Coast::None;
    return true;
}

bool MotorProtection::IsCoasting() const
{
    return status_.coast != Coast::None;
}

const MotorProtection::Status &MotorProtection::GetStatus() const
{
    return status_;
}
