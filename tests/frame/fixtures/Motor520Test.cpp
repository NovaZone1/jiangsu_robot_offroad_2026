#include "SyntheticMotorPid.hpp"
#include "Motor520Test.hpp"
// Host-only timing regression fixture. Production protection is MotorProtection.
#include "MotorSpeedProfiles.hpp"
#include "stm32f1xx_hal.h"
#include <math.h>

namespace
{
    constexpr uint32_t control_gap_ms = 50;
    constexpr uint32_t direction_grace_ms = 500;
    constexpr uint32_t no_motion_ms = 1500;
    constexpr uint32_t standstill_ms = 100;
    constexpr float motion_rpm = 3.0f;
    constexpr float standstill_rpm = 6.0f;
    constexpr float overspeed_rpm = 400.0f; // 320 RPM 空载标称值之外留余量。
} // namespace

Motor520Test::Motor520Test(DcMotor &m1, DcMotor &m2, DcMotor &m3, DcMotor &m4, uint8_t port_mask)
    : motors_{&m1, &m2, &m3, &m4}, port_mask_(port_mask)
{
    status_.port_mask = port_mask;
}

void Motor520Test::StopAll()
{
    for (auto motor : motors_)
    {
        motor->Disable();
    }
    status_.target_rpm = 0;
}

void Motor520Test::Fail(Failure failure, uint8_t port)
{
    StopAll();
    status_.phase = Phase::Failed;
    status_.failure = failure;
    status_.failed_port = port;
}

bool Motor520Test::Start()
{
    if (status_.phase != Phase::Idle)
    {
        return false;
    }
    StopAll();
    if (port_mask_ == 0 || (port_mask_ & ~0x0FU) != 0)
    {
        Fail(Failure::InvalidPorts);
        return false;
    }
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (IsSelected(i) && !motors_[i]->IsReady())
        {
            Fail(Failure::MotorNotReady, i + 1U);
            return false;
        }
    }
    phase_tick_ = last_tick_ = HAL_GetTick();
    status_.phase = Phase::Waiting;
    return true;
}

bool Motor520Test::IsMovingPhase() const
{
    return status_.phase == Phase::ForwardSlow || status_.phase == Phase::ForwardFast ||
           status_.phase == Phase::ReverseSlow || status_.phase == Phase::ReverseFast;
}

bool Motor520Test::IsSelected(uint8_t index) const
{
    return (port_mask_ & (1U << index)) != 0;
}

bool Motor520Test::Begin(Phase phase, uint32_t now, bool apply_profile)
{
    const bool backward = phase == Phase::ReverseSlow || phase == Phase::ReverseFast;
    const bool fast = phase == Phase::ForwardFast || phase == Phase::ReverseFast;
    if (apply_profile)
    {
        StopAll();
        for (uint8_t i = 0; i < 4; ++i)
        {
            if (!IsSelected(i))
            {
                continue;
            }
            if (!MotorSpeedProfiles::Apply(*motors_[i], i + 1U, backward) ||
                !ConfigureSyntheticMotorPid(*motors_[i]) || !motors_[i]->Enable())
            {
                Fail(Failure::MotorNotReady, i + 1U);
                return false;
            }
        }
    }
    status_.phase = phase;
    status_.target_rpm = (backward ? -1.0f : 1.0f) * (fast ? fast_rpm : slow_rpm);
    status_.phase_elapsed_ms = 0;
    phase_tick_ = now;
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (!IsSelected(i))
        {
            continue;
        }
        last_motion_tick_[i] = now;
        sample_count_[i] = motors_[i]->GetMeasure().sample_count;
        wrong_samples_[i] = 0;
        if (!motors_[i]->SetSpeed(status_.target_rpm))
        {
            Fail(Failure::MotorFault, i + 1U);
            return false;
        }
    }
    return true;
}

bool Motor520Test::CheckHealth(uint32_t now)
{
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (!IsSelected(i))
        {
            continue;
        }
        const auto &motor = *motors_[i];
        if (motor.GetFault() != DcMotor::Fault::None)
        {
            Fail(motor.GetFault() == DcMotor::Fault::FeedbackLost ? Failure::FeedbackLost
                                                                  : Failure::MotorFault,
                 i + 1U);
            return false;
        }
        if (!motor.IsOnline())
        {
            Fail(Failure::FeedbackLost, i + 1U);
            return false;
        }
        const auto &measure = motor.GetMeasure();
        if (sample_count_[i] == measure.sample_count)
        {
            continue;
        }
        sample_count_[i] = measure.sample_count;
        const float rpm = measure.speed_rpm;
        if (fabsf(rpm) > overspeed_rpm)
        {
            Fail(Failure::Overspeed, i + 1U);
            return false;
        }
        if (!IsMovingPhase())
        {
            continue;
        }
        if (fabsf(rpm) >= motion_rpm)
        {
            last_motion_tick_[i] = now;
        }
        if ((uint32_t)(now - last_motion_tick_[i]) >= no_motion_ms)
        {
            Fail(Failure::NoMotion, i + 1U);
            return false;
        }
        if (status_.phase_elapsed_ms >= direction_grace_ms &&
            rpm * status_.target_rpm < -motion_rpm * fabsf(status_.target_rpm))
        {
            if (++wrong_samples_[i] >= 3)
            {
                Fail(Failure::WrongDirection, i + 1U);
                return false;
            }
        }
        else
        {
            wrong_samples_[i] = 0;
        }
    }
    return true;
}

void Motor520Test::Update()
{
    if (status_.phase == Phase::Idle || status_.phase == Phase::Finished ||
        status_.phase == Phase::Failed)
    {
        return;
    }
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_tick_) >= control_gap_ms)
    {
        Fail(Failure::ControlGap);
        return;
    }
    last_tick_ = now;
    status_.phase_elapsed_ms = now - phase_tick_;
    // 先处理阶段结束，避免在 5 秒边界继续输出上一阶段的目标。
    if (IsMovingPhase() && status_.phase_elapsed_ms >= stage_ms)
    {
        if (status_.phase == Phase::ForwardSlow)
        {
            if (!Begin(Phase::ForwardFast, now, false))
            {
                return;
            }
        }
        else if (status_.phase == Phase::ForwardFast)
        {
            StopAll();
            status_.phase = Phase::Coasting;
            phase_tick_ = now;
            status_.phase_elapsed_ms = 0;
            still_ = false;
        }
        else if (status_.phase == Phase::ReverseSlow)
        {
            if (!Begin(Phase::ReverseFast, now, false))
            {
                return;
            }
        }
        else
        {
            StopAll();
            status_.phase = Phase::Finished;
            return;
        }
    }
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (IsSelected(i) && IsMovingPhase() && !motors_[i]->SetSpeed(status_.target_rpm))
        {
            Fail(Failure::MotorFault, i + 1U);
            return;
        }
        motors_[i]->Control();
    }
    if (status_.phase == Phase::Waiting && status_.phase_elapsed_ms < startup_ms)
    {
        return;
    }
    if (!CheckHealth(now))
    {
        return;
    }
    if (status_.phase == Phase::Waiting)
    {
        Begin(Phase::ForwardSlow, now, true);
    }
    else if (status_.phase == Phase::Coasting)
    {
        bool stopped = true;
        for (uint8_t i = 0; i < 4; ++i)
        {
            if (IsSelected(i))
            {
                stopped = stopped && fabsf(motors_[i]->GetMeasure().speed_rpm) < standstill_rpm;
            }
        }
        if (stopped && !still_)
        {
            still_ = true;
            still_tick_ = now;
        }
        else if (!stopped)
        {
            still_ = false;
        }
        if (status_.phase_elapsed_ms >= coast_min_ms && still_ &&
            (uint32_t)(now - still_tick_) >= standstill_ms)
        {
            Begin(Phase::ReverseSlow, now, true);
        }
        else if (status_.phase_elapsed_ms >= coast_timeout_ms)
        {
            Fail(Failure::CoastTimeout);
        }
    }
}

const Motor520Test::Status &Motor520Test::GetStatus() const
{
    return status_;
}
