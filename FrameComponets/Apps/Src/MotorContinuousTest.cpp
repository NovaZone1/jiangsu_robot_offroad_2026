#include "MotorContinuousTest.hpp"
#include "stm32f1xx_hal.h"

MotorContinuousTest::MotorContinuousTest(DcMotor &m1, DcMotor &m2, DcMotor &m3, DcMotor &m4,
                                         MotorProtection &protection)
    : motors_{&m1, &m2, &m3, &m4}, protection_(protection)
{
}

bool MotorContinuousTest::Fail(Failure failure)
{
    for (auto motor : motors_)
    {
        motor->Disable();
    }
    if (status_.phase != Phase::Fault)
    {
        status_.failure = failure;
        status_.phase = Phase::Fault;
    }
    return false;
}

bool MotorContinuousTest::Init()
{
    if (status_.phase != Phase::Disarmed ||
        protection_.GetStatus().fault != MotorProtection::Fault::None)
    {
        return Fail(Failure::Initialization);
    }
    for (auto motor : motors_)
    {
        if (!motor->IsReady() || motor->IsEnabled() || motor->GetFault() != DcMotor::Fault::None)
        {
            return Fail(Failure::Initialization);
        }
    }
    auto pid = motors_[SelectedIndex]->GetSpeedPidConfig();
    pid.derivative_on_measurement = true;
    pid.integral_limit = DutyLimit;
    if (motors_[SelectedIndex]->GetMode() != DcMotor::Mode::Speed ||
        !motors_[SelectedIndex]->ConfigureSpeedPid(pid) ||
        !motors_[SelectedIndex]->DutyLimSet(DutyLimit))
    {
        return Fail(Failure::Initialization);
    }
    start_tick_ = HAL_GetTick();
    status_.phase = Phase::Waiting;
    return true;
}

bool MotorContinuousTest::Run()
{
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (i != SelectedIndex)
        {
            motors_[i]->Disable();
        }
    }
    if (status_.phase == Phase::Disarmed || status_.phase == Phase::Fault)
    {
        motors_[SelectedIndex]->Disable();
        return false;
    }
    if (!protection_.BeforeControl())
    {
        return Fail(Failure::Protection);
    }
    auto &motor = *motors_[SelectedIndex];
    if (status_.phase == Phase::Waiting)
    {
        motor.Disable();
        const uint32_t elapsed = HAL_GetTick() - start_tick_;
        // First acquire encoder samples, then use the original standstill interlock.
        if (elapsed >= 100 && !protection_.IsCoasting() && !protection_.RequestCoastStop())
        {
            return Fail(Failure::Start);
        }
        if (elapsed >= 2000 && protection_.CanResumeAfterCoast())
        {
            if (!protection_.ReleaseCoastStop() || !motor.Enable())
            {
                return Fail(Failure::Start);
            }
            status_.phase = Phase::Running;
        }
    }
    if (status_.phase == Phase::Running)
    {
        // Never automatically re-enable following any external stop or motor fault.
        if (!motor.IsEnabled())
        {
            return Fail(Failure::UnexpectedDisable);
        }
        if (!motor.SetSpeed(TargetRpm))
        {
            return Fail(Failure::Start);
        }
    }
    DcMotor::ControlAllMotors();
    if (motor.IsEnabled())
    {
        status_.target_rpm = motor.GetTargetSpeed();
        status_.measured_rpm = motor.GetMeasure().speed_rpm;
        status_.duty = motor.GetDuty();
        status_.sample_tick = HAL_GetTick();
    }
    if (!protection_.AfterControl())
    {
        return Fail(Failure::Protection);
    }
    return true;
}

void MotorContinuousTest::Stop()
{
    Fail(Failure::ExternalStop);
}

const MotorContinuousTest::Status &MotorContinuousTest::GetStatus() const
{
    return status_;
}
