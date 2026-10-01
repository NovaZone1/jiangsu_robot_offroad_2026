#include "dc_motor.hpp"

#include "stm32f1xx_hal.h"
#include <math.h>

namespace
{
    float Clamp(float value, float limit)
    {
        return value > limit ? limit : (value < -limit ? -limit : value);
    }
} // namespace

DcMotor *DcMotor::motors_[FRAME_MAX_MOTORS] = {};

DcMotor::~DcMotor()
{
    Disable();
    for (auto &motor : motors_)
    {
        if (motor == this)
        {
            motor = nullptr;
        }
    }
}

bool DcMotor::Register()
{
    for (auto motor : motors_)
    {
        if (motor == this)
        {
            return true;
        }
    }
    for (auto &motor : motors_)
    {
        if (!motor)
        {
            motor = this;
            return true;
        }
    }
    return false;
}

bool DcMotor::Init(const Driver &driver, Mode mode)
{
    Disable();
    driver_ = {};
    measure_ = {};
    // 重新绑定设备不继承旧设备的闭环参数。
    duty_limit_ = 1;
    speed_limit_ = 0;
    command_timeout_ms_ = 250;
    feedback_timeout_ms_ = 50;
    pid_configured_ = false;
    feedback_filter_tau_s_ = filtered_rpm_ = 0;
    filter_valid_ = false;
    speed_pid_.Init(0, 0, 0);
    if (!driver.set_duty || !driver.stop || (mode == Mode::Speed && !driver.read_rpm) ||
        (mode != Mode::Duty && mode != Mode::Speed) || !Register())
    {
        return false;
    }
    driver_ = driver;
    mode_ = mode;
    fault_ = Fault::None;
    measure_ = {};
    Neutral();
    return true;
}

bool DcMotor::Bind(const Driver &driver)
{
    return Init(driver, Mode::Duty);
}

bool DcMotor::IsReady() const
{
    return driver_.set_duty && driver_.stop && fault_ == Fault::None;
}

bool DcMotor::Enable()
{
    if (!IsReady())
    {
        return false;
    }
    const uint32_t now = HAL_GetTick();
    if (mode_ == Mode::Speed)
    {
        UpdateFeedback(now);
        if (!pid_configured_ || speed_limit_ <= 0 || !IsOnline())
        {
            return false;
        }
    }
    Neutral();
    enabled_ = true;
    pid_tick_ = mode_ == Mode::Speed ? measure_.timestamp_ms : now;
    return true;
}

void DcMotor::Disable()
{
    enabled_ = false;
    Neutral();
}

bool DcMotor::IsEnabled() const
{
    return enabled_;
}

void DcMotor::Neutral()
{
    has_command_ = false;
    target_speed_ = target_duty_ = duty_ = 0;
    speed_pid_.Reset();
    if (driver_.stop)
    {
        driver_.stop(driver_.context);
    }
}

void DcMotor::Stop()
{
    Neutral();
}

bool DcMotor::SwitchMode(Mode mode)
{
    Disable();
    if (mode != Mode::Duty && mode != Mode::Speed)
    {
        return false;
    }
    if (mode == Mode::Speed && !driver_.read_rpm)
    {
        return false;
    }
    mode_ = mode;
    return true;
}

void DcMotor::Trip(Fault fault)
{
    Disable();
    fault_ = fault;
}

bool DcMotor::ClearFault()
{
    if (enabled_)
    {
        return false;
    }
    Neutral();
    measure_.valid = false;
    fault_ = Fault::None;
    return true;
}

bool DcMotor::WriteOutput(float duty)
{
    if (!isfinite(duty) || !driver_.set_duty ||
        !driver_.set_duty(driver_.context, Clamp(duty, duty_limit_)))
    {
        Trip(Fault::OutputFailure);
        return false;
    }
    duty_ = Clamp(duty, duty_limit_);
    return true;
}

bool DcMotor::SetDuty(float signed_duty)
{
    if (!isfinite(signed_duty))
    {
        Trip(Fault::InvalidCommand);
        return false;
    }
    if (!IsReady() || !enabled_ || mode_ != Mode::Duty)
    {
        return false;
    }
    target_duty_ = Clamp(signed_duty, duty_limit_);
    command_tick_ = HAL_GetTick();
    has_command_ = true;
    return WriteOutput(target_duty_);
}

bool DcMotor::SetSpeed(float output_rpm)
{
    if (!isfinite(output_rpm))
    {
        Trip(Fault::InvalidCommand);
        return false;
    }
    if (!IsReady() || !enabled_ || mode_ != Mode::Speed || speed_limit_ <= 0)
    {
        return false;
    }
    target_speed_ = Clamp(output_rpm, speed_limit_);
    command_tick_ = HAL_GetTick();
    has_command_ = true;
    return true;
}

bool DcMotor::DutyLimSet(float limit)
{
    if (!isfinite(limit) || limit <= 0 || limit > 1)
    {
        return false;
    }
    Neutral();
    duty_limit_ = limit;
    return true;
}

bool DcMotor::SpeedLimSet(float output_rpm)
{
    if (!isfinite(output_rpm) || output_rpm <= 0)
    {
        return false;
    }
    Neutral();
    speed_limit_ = output_rpm;
    return true;
}

bool DcMotor::ConfigureSpeedPid(const SpeedPidConfig &config)
{
    if (enabled_ || !isfinite(config.kp) || !isfinite(config.ki) || !isfinite(config.kd) ||
        config.kp < 0 || config.ki < 0 || config.kd < 0 || !isfinite(config.integral_limit) ||
        config.integral_limit <= 0 || config.integral_limit > 1 ||
        !isfinite(config.derivative_filter) || config.derivative_filter < 0 ||
        config.derivative_filter > 1 || !isfinite(config.feedback_filter_tau_s) ||
        config.feedback_filter_tau_s < 0 || config.feedback_filter_tau_s > 0.2f)
    {
        return false;
    }
    speed_pid_.Init(config.kp, config.ki, config.kd);
    speed_pid_.SetLimit(config.integral_limit, 0, config.derivative_filter);
    feedback_filter_tau_s_ = config.feedback_filter_tau_s;
    filter_valid_ = false;
    pid_configured_ = config.kp > 0 || config.ki > 0 || config.kd > 0;
    return true;
}

bool DcMotor::SetTimeouts(uint32_t command_ms, uint32_t feedback_ms)
{
    if (enabled_ || command_ms == 0 || feedback_ms == 0 || command_ms >= 0x80000000U ||
        feedback_ms >= 0x80000000U)
    {
        return false;
    }
    command_timeout_ms_ = command_ms;
    feedback_timeout_ms_ = feedback_ms;
    return true;
}

bool DcMotor::UpdateFeedback(uint32_t now)
{
    float rpm = 0;
    if (!driver_.read_rpm || !driver_.read_rpm(driver_.context, &rpm))
    {
        return false;
    }
    if (!isfinite(rpm))
    {
        measure_.valid = false;
        filter_valid_ = false;
        return false;
    }
    const float dt = (float)(now - measure_.timestamp_ms) * 0.001f;
    if (!filter_valid_ || feedback_filter_tau_s_ == 0)
    {
        filtered_rpm_ = rpm;
    }
    else
    {
        filtered_rpm_ += dt / (feedback_filter_tau_s_ + dt) * (rpm - filtered_rpm_);
    }
    filter_valid_ = true;
    measure_.speed_rpm = rpm;
    measure_.timestamp_ms = now;
    ++measure_.sample_count;
    measure_.valid = true;
    return true;
}

bool DcMotor::IsOnline() const
{
    return measure_.valid &&
           (uint32_t)(HAL_GetTick() - measure_.timestamp_ms) < feedback_timeout_ms_;
}

bool DcMotor::ReadRpm(float &rpm)
{
    UpdateFeedback(HAL_GetTick());
    if (!IsOnline())
    {
        return false;
    }
    rpm = measure_.speed_rpm;
    return true;
}

float DcMotor::Control()
{
    const uint32_t now = HAL_GetTick();
    UpdateFeedback(now);
    if (!enabled_ || !IsReady() || !has_command_)
    {
        return duty_;
    }

    if ((uint32_t)(now - command_tick_) >= command_timeout_ms_)
    {
        Trip(Fault::CommandTimeout);
        return 0;
    }
    if (mode_ == Mode::Duty)
    {
        WriteOutput(target_duty_);
        return duty_;
    }
    if (!IsOnline())
    {
        Trip(Fault::FeedbackLost);
        return 0;
    }

    // 一次新反馈只推进一次 PID；ReadRpm() 取走新样本后仍按时间戳识别。
    if (measure_.timestamp_ms != pid_tick_)
    {
        const uint32_t elapsed = measure_.timestamp_ms - pid_tick_;
        pid_tick_ = measure_.timestamp_ms;
        speed_pid_.ManualDt((float)elapsed * 0.001f);
        const float previous_integral = speed_pid_.inte_errors;
        const float output = speed_pid_.Calc(target_speed_, filtered_rpm_);
        if (!isfinite(output))
        {
            Trip(Fault::OutputFailure);
            return 0;
        }
        // 饱和时不再向同一方向累积积分，避免长时间限幅后反向迟滞。
        if (fabsf(output) >= duty_limit_ && output * speed_pid_.error > 0)
        {
            speed_pid_.inte_errors = previous_integral;
        }
        WriteOutput(output);
    }
    else
    {
        // 维持输出，同时推进硬件适配器的非阻塞换向状态。
        WriteOutput(duty_);
    }
    return duty_;
}

void DcMotor::ControlAllMotors()
{
    for (auto motor : motors_)
    {
        if (motor)
        {
            motor->Control();
        }
    }
}

const DcMotor::Measure &DcMotor::GetMeasure() const
{
    return measure_;
}

DcMotor::Mode DcMotor::GetMode() const
{
    return mode_;
}

DcMotor::Fault DcMotor::GetFault() const
{
    return fault_;
}

float DcMotor::GetDuty() const
{
    return duty_;
}

float DcMotor::GetTargetSpeed() const
{
    return target_speed_;
}

float DcMotor::GetFilteredRpm() const
{
    return filtered_rpm_;
}
