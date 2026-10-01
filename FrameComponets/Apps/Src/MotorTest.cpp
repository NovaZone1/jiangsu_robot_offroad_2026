#include "MotorTest.hpp"

#include "stm32f1xx_hal.h"
#include <math.h>

namespace
{
    constexpr uint32_t startup_ms = 2000;
    constexpr uint32_t movement_ms = 5000;
    constexpr uint32_t direction_grace_ms = 500;
    constexpr uint32_t no_motion_ms = 1500;
} // namespace

MotorTest::MotorTest(DcMotor &first, DcMotor &second, DcMotor &third, DcMotor &fourth)
    : motors_{&first, &second, &third, &fourth}
{
}

void MotorTest::StopAll()
{
    for (auto motor : motors_)
    {
        motor->Disable();
    }
    status_.target_rpm = 0;
}

void MotorTest::Fail(Failure reason, uint8_t port)
{
    StopAll();
    status_.phase = Phase::Failed;
    status_.failure = reason;
    status_.failed_port = port;
}

bool MotorTest::Start()
{
    return Start(Config{});
}

bool MotorTest::Start(const Config &config)
{
    // 每次上电仅执行一次；完成或故障后都不能悄悄重新启动。
    if (status_.phase != Phase::Idle)
    {
        return false;
    }
    StopAll();
    if (!isfinite(config.target_rpm) || config.target_rpm <= 0 ||
        !isfinite(config.speed_limit_rpm) || config.target_rpm > config.speed_limit_rpm)
    {
        Fail(Failure::Setup, 0);
        return false;
    }
    config_ = config;
    DcMotor::SpeedPidConfig pid;
    pid.kp = config.kp;
    pid.ki = config.ki;
    pid.kd = config.kd;
    pid.integral_limit = config.integral_limit;
    for (uint8_t index = 0; index < 4; ++index)
    {
        auto motor = motors_[index];
        if (!motor->IsReady() || !motor->SwitchMode(DcMotor::Mode::Speed) ||
            !motor->ConfigureSpeedPid(pid) || !motor->DutyLimSet(config.duty_limit) ||
            !motor->SpeedLimSet(config.speed_limit_rpm) || !motor->SetTimeouts(250, 50))
        {
            Fail(Failure::Setup, index + 1U);
            return false;
        }
    }
    status_.phase = Phase::WaitingForFeedback;
    phase_tick_ = HAL_GetTick();
    last_update_tick_ = phase_tick_;
    return true;
}

void MotorTest::BeginPhase(Phase phase, uint32_t now)
{
    status_.phase = phase;
    status_.elapsed_ms = 0;
    status_.target_rpm = phase == Phase::Forward ? config_.target_rpm : -config_.target_rpm;
    phase_tick_ = now;
    for (uint8_t index = 0; index < 4; ++index)
    {
        motors_[index]->Neutral(); // 换向时清除前一方向的积分及目标。
        motion_tick_[index] = now;
        sample_count_[index] = motors_[index]->GetMeasure().sample_count;
        wrong_direction_count_[index] = 0;
    }
}

bool MotorTest::CheckMotion(uint32_t now, uint8_t index)
{
    auto motor = motors_[index];
    if (!motor->IsEnabled() || !motor->IsReady())
    {
        Fail(Failure::MotorFault, index + 1U);
        return false;
    }
    if (!motor->IsOnline())
    {
        Fail(Failure::FeedbackUnavailable, index + 1U);
        return false;
    }
    const auto &sample = motor->GetMeasure();
    if (sample.sample_count != sample_count_[index])
    {
        sample_count_[index] = sample.sample_count;
        // 1040 CPR / 10 ms 的单计数分辨率约 5.77 RPM。
        if (fabsf(sample.speed_rpm) >= 3)
        {
            motion_tick_[index] = now;
        }
        // 留出换向减速时间，之后连续三个新样本方向相反则停止全部电机。
        if ((uint32_t)(now - phase_tick_) >= direction_grace_ms && fabsf(sample.speed_rpm) >= 3 &&
            sample.speed_rpm * status_.target_rpm < 0)
        {
            if (++wrong_direction_count_[index] >= 3)
            {
                Fail(Failure::WrongDirection, index + 1U);
                return false;
            }
        }
        else
        {
            wrong_direction_count_[index] = 0;
        }
    }
    if ((uint32_t)(now - motion_tick_[index]) >= no_motion_ms && fabsf(motor->GetDuty()) >= 0.1f)
    {
        Fail(Failure::NoMotion, index + 1U);
        return false;
    }
    return true;
}

void MotorTest::Update()
{
    if (status_.phase == Phase::Idle || status_.phase == Phase::Finished ||
        status_.phase == Phase::Failed)
    {
        return;
    }
    const uint32_t now = HAL_GetTick();
    status_.elapsed_ms = now - phase_tick_;
    if ((status_.phase == Phase::Forward || status_.phase == Phase::Reverse) &&
        (uint32_t)(now - last_update_tick_) >= 50U)
    {
        Fail(Failure::ControlTimeout, 0);
        return;
    }
    last_update_tick_ = now;
    if (status_.phase == Phase::WaitingForFeedback)
    {
        for (auto motor : motors_)
        {
            motor->Control(); // 禁能采样，上电等待期间没有驱动输出。
        }
        if (status_.elapsed_ms < startup_ms)
        {
            return;
        }
        for (uint8_t index = 0; index < 4; ++index)
        {
            if (!motors_[index]->IsOnline())
            {
                Fail(Failure::FeedbackUnavailable, index + 1U);
                return;
            }
            if (!motors_[index]->Enable())
            {
                Fail(Failure::Enable, index + 1U);
                return;
            }
        }
        BeginPhase(Phase::Forward, now);
    }
    else if (status_.elapsed_ms >= movement_ms)
    {
        if (status_.phase == Phase::Forward)
        {
            BeginPhase(Phase::Reverse, now);
        }
        else
        {
            StopAll();
            status_.phase = Phase::Finished;
            return;
        }
    }

    for (uint8_t index = 0; index < 4; ++index)
    {
        if (!motors_[index]->SetSpeed(status_.target_rpm))
        {
            Fail(Failure::MotorFault, index + 1U);
            return;
        }
        motors_[index]->Control();
        if (!CheckMotion(now, index))
        {
            return;
        }
    }
}

const MotorTest::Status &MotorTest::GetStatus() const
{
    return status_;
}
