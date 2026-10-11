#include "NovaLink.hpp"
#include "MainFrame.hpp"
#include "LineFollowBoard.hpp"
#include <math.h>

NovaLinkApp NovaLink;

namespace
{
    DcMotor *const motors[] = {&Motor1, &Motor2, &Motor3, &Motor4};

    bool Stopped()
    {
        if (System.GetState() == Systems::WORKING ||
            (!NovaLinkApp::M1TuningEnabled && MotorSafety.IsCoasting()))
        {
            return false;
        }
        for (DcMotor *motor : motors)
        {
            if (motor->IsEnabled())
            {
                return false;
            }
        }
        return true;
    }
} // namespace

bool NovaLinkApp::Init()
{
    if (initialized_)
    {
        return true;
    }
    NovaProtocol::Callbacks callbacks;
    callbacks.context = this;
    callbacks.send = Send;
    callbacks.execute = Execute;
    initialized_ = bluetooth_.Init(Baud) && protocol_.Init(callbacks);
    last_tick_ = HAL_GetTick();
    uptime_ms_ = last_tick_;
    last_publish_ = last_tick_;
    return initialized_;
}

bool NovaLinkApp::Send(void *context, const uint8_t *data, uint16_t size)
{
    return static_cast<NovaLinkApp *>(context)->bluetooth_.Send(data, size);
}

bool NovaLinkApp::Execute(void *context, const NovaProtocol::Request &request,
                          NovaProtocol::Reply &reply)
{
    auto &self = *static_cast<NovaLinkApp *>(context);
    using Command = NovaProtocol::Command;
    if (request.channel < 1 || request.channel > 5 ||
        (request.channel == 5 && !LineFollowBoard::Enabled))
    {
        reply.error = "unknown_channel";
        return false;
    }
    // STOP is always honored, including fault states; it never clears a protection latch.
    if (request.command == Command::Stop)
    {
        if (M1TuningEnabled)
        {
            self.StopTrial();
            return true;
        }
        System.Stop(System.GetState() == Systems::ERROR ||
                    MotorSafety.GetStatus().fault != MotorProtection::Fault::None);
        LineFollow.StopMotors();
        for (DcMotor *motor : motors)
        {
            motor->Disable();
        }
        return true;
    }
    const bool motor_channel = request.channel <= 4;
    DcMotor *motor = motor_channel ? motors[request.channel - 1U] : nullptr;
    if (motor && !motor->IsReady())
    {
        reply.error = "not_ready";
        return false;
    }
    if (request.command == Command::Configure || request.command == Command::Heartbeat ||
        request.command == Command::SetTarget)
    {
        return self.TrialCommand(request, reply);
    }
    if (request.command == Command::SetPid)
    {
        if (M1TuningEnabled && request.channel != 1)
        {
            reply.error = "m1_only";
            return false;
        }
        if (M1TuningEnabled && (self.trial_fault_ || System.GetState() == Systems::ERROR))
        {
            reply.error = self.trial_fault_ ? self.trial_fault_ : "system_fault";
            return false;
        }
        if (!Stopped())
        {
            reply.error = "stop_and_disable_first";
            return false;
        }
        if (MotorSafety.GetStatus().fault != MotorProtection::Fault::None)
        {
            reply.error = "motor_protection_latched";
            return false;
        }
        const auto &g = request.gains;
        if (motor)
        {
            // Bounded admission policy, not an assertion that these gains are well tuned.
            if (g.kp > 0.1f || g.ki > 1.0f || g.kd > 0.01f)
            {
                reply.error = "gain_limit";
                return false;
            }
            auto config = motor->GetSpeedPidConfig();
            config.kp = g.kp;
            config.ki = g.ki;
            config.kd = g.kd;
            if (M1TuningEnabled)
            {
                config.derivative_on_measurement = true;
            }
            if (!motor->ConfigureSpeedPid(config))
            {
                reply.error = "invalid_PID";
                return false;
            }
            motor->Neutral();
        }
        else if (!LineFollow.ConfigureTrackPid(g.kp, g.ki, g.kd))
        {
            reply.error = "invalid_PID";
            return false;
        }
    }
    if (motor)
    {
        const auto &config = motor->GetSpeedPidConfig();
        reply.gains.kp = config.kp;
        reply.gains.ki = config.ki;
        reply.gains.kd = config.kd;
        reply.target = motor->GetTargetSpeed();
        reply.enabled = motor->IsEnabled();
        reply.period = 0.01f;
    }
    else
    {
        const auto &config = LineFollow.GetTrackConfig();
        reply.gains.kp = config.kp;
        reply.gains.ki = config.ki;
        reply.gains.kd = config.kd;
        reply.target = 0;
        reply.enabled = System.GetState() == Systems::WORKING;
        reply.period = 0.005f;
    }
    // Existing PidGeneral differentiates error. Never advertise Nova's different D-on-PV model.
    reply.algorithm = "positional_pid_d_on_error";
    reply.capabilities = NovaProtocol::SetPid | NovaProtocol::Stop;
    if (M1TuningEnabled && request.channel == 1)
    {
        reply.algorithm = "positional_pid_d_on_measurement";
        reply.capabilities |=
            NovaProtocol::SetTarget | NovaProtocol::Watchdog | NovaProtocol::Limits;
    }
    return true;
}

void NovaLinkApp::StopTrial()
{
    const bool moving_trial = trial_started_ || Motor1.IsEnabled();
    System.Stop(System.GetState() == Systems::ERROR ||
                MotorSafety.GetStatus().fault != MotorProtection::Fault::None);
    for (auto motor : motors)
    {
        motor->Disable();
    }
    trial_started_ = false;
    if (moving_trial && !MotorSafety.IsCoasting())
    {
        MotorSafety.RequestCoastStop();
    }
}

void NovaLinkApp::FailTrial(const char *reason)
{
    if (!trial_fault_)
    {
        trial_fault_ = reason;
    }
    StopTrial();
    configured_ = false;
}

void NovaLinkApp::CheckTrial()
{
    if (!M1TuningEnabled)
    {
        return;
    }
    for (unsigned i = 1; i < 4; ++i)
    {
        if (motors[i]->IsEnabled() && configured_)
        {
            FailTrial("other_motor_enabled");
        }
        motors[i]->Disable();
    }
    if (MotorSafety.GetStatus().fault != MotorProtection::Fault::None ||
        System.GetState() == Systems::ERROR)
    {
        FailTrial("motor_or_system_protection");
    }
    if (trial_fault_)
    {
        Motor1.Disable();
        if (!trial_fault_sent_)
        {
            trial_fault_sent_ = protocol_.Fault(1, trial_fault_);
        }
        return;
    }
    const uint32_t now = HAL_GetTick();
    if (configured_ && (uint32_t)(now - heartbeat_tick_) >= watchdog_ms_)
    {
        if (trial_started_ || Motor1.IsEnabled())
        {
            FailTrial("heartbeat_timeout");
        }
        else
        {
            configured_ = false; // Completed/reviewed session, already disabled; require configure.
        }
        return;
    }
    if (!configured_)
    {
        Motor1.Disable();
        return;
    }
    if (trial_started_ && (uint32_t)(now - trial_tick_) >= 45000U)
    {
        FailTrial("trial_timeout");
        return;
    }
    if (!Motor1.IsOnline() || Motor1.GetFault() != DcMotor::Fault::None)
    {
        FailTrial("feedback_or_driver_fault");
        return;
    }
    const float raw = Motor1.GetMeasure().speed_rpm;
    const float filtered = Motor1.GetFilteredRpm();
    if (raw < actual_min_ || raw > actual_max_ || filtered < actual_min_ || filtered > actual_max_)
    {
        FailTrial("actual_limit");
        return;
    }
    if (Motor1.IsEnabled() && !Motor1.SetSpeed(Motor1.GetTargetSpeed()))
    {
        FailTrial("target_refresh_failed");
    }
}

void NovaLinkApp::AfterControl()
{
    // Called immediately after feedback/control/protection, not only at the next telemetry period.
    CheckTrial();
}

bool NovaLinkApp::TrialCommand(const NovaProtocol::Request &request, NovaProtocol::Reply &reply)
{
    using Command = NovaProtocol::Command;
    if (!M1TuningEnabled || request.channel != 1)
    {
        reply.error = "m1_tuning_only";
        return false;
    }
    if (trial_fault_ || MotorSafety.GetStatus().fault != MotorProtection::Fault::None ||
        System.GetState() == Systems::ERROR)
    {
        reply.error = trial_fault_ ? trial_fault_ : "protection_latched";
        return false;
    }
    const uint32_t now = HAL_GetTick();
    if (request.command == Command::Configure)
    {
        if (!Stopped())
        {
            reply.error = "stop_and_disable_first";
            return false;
        }
        // Device limits cannot be loosened by a generic Nova profile.
        if (!(request.actual_min >= -180 && request.actual_min < 0 && request.actual_max > 0 &&
              request.actual_max <= 180 && request.output_limit > 0 &&
              request.output_limit <= 0.69f && request.watchdog >= 0.8f &&
              request.watchdog <= 2.0f))
        {
            reply.error = "unsafe_limits";
            return false;
        }
        if (!Motor1.IsOnline())
        {
            reply.error = "feedback_not_ready";
            return false;
        }
        auto config = Motor1.GetSpeedPidConfig();
        config.derivative_on_measurement = true;
        config.integral_limit = request.output_limit;
        if (!Motor1.ConfigureSpeedPid(config) || !Motor1.DutyLimSet(request.output_limit))
        {
            reply.error = "configuration_failed";
            return false;
        }
        actual_min_ = request.actual_min;
        actual_max_ = request.actual_max;
        watchdog_ms_ = (uint32_t)(request.watchdog * 1000);
        heartbeat_tick_ = now;
        configured_ = true;
        trial_started_ = false;
        // Initial arming also requires the existing 1 s + standstill coast interlock.
        if (!MotorSafety.IsCoasting())
        {
            MotorSafety.RequestCoastStop();
        }
        return true;
    }
    if (!configured_)
    {
        reply.error = "configure_required";
        return false;
    }
    if (request.command == Command::Heartbeat)
    {
        heartbeat_tick_ = now;
        return true;
    }
    if (!isfinite(request.value) || fabsf(request.value) > MaximumTrialRpm ||
        request.value < actual_min_ || request.value > actual_max_)
    {
        reply.error = "target_limit";
        return false;
    }
    if (request.value == 0)
    {
        // Zero target is neutral, never active braking. Retain the trial deadline.
        if (Motor1.IsEnabled() && !MotorSafety.IsCoasting())
        {
            MotorSafety.RequestCoastStop();
        }
        Motor1.Disable();
    }
    else
    {
        if (MotorSafety.IsCoasting() && !MotorSafety.CanResumeAfterCoast())
        {
            reply.error = "waiting_for_standstill";
            return false;
        }
        if (Motor1.IsEnabled() && Motor1.GetTargetSpeed() * request.value < 0)
        {
            reply.error = "stop_before_reverse";
            return false;
        }
        if (!Motor1.IsEnabled())
        {
            if (!Motor1.IsOnline() || fabsf(Motor1.GetMeasure().speed_rpm) >= 6)
            {
                reply.error = "not_stationary";
                return false;
            }
            if (MotorSafety.IsCoasting() && !MotorSafety.ReleaseCoastStop())
            {
                reply.error = "coast_interlock";
                return false;
            }
            if (!Motor1.Enable())
            {
                reply.error = "enable_failed";
                return false;
            }
        }
        if (!Motor1.SetSpeed(request.value))
        {
            FailTrial("set_target_failed");
            reply.error = "set_target_failed";
            return false;
        }
    }
    if (!trial_started_)
    {
        trial_started_ = true;
        trial_tick_ = now;
    }
    reply.target = request.value;
    return true;
}

void NovaLinkApp::Service()
{
    if (!initialized_)
    {
        return;
    }
    const uint32_t now = HAL_GetTick();
    uptime_ms_ += (uint32_t)(now - last_tick_);
    last_tick_ = now;
    CheckTrial(); // Expire the old lease before processing any queued commands.
    bluetooth_.Service();
    const uint32_t tx_errors = bluetooth_.GetStats().tx_errors;
    if (configured_ && tx_errors != tx_errors_)
    {
        FailTrial("uart_tx_error");
    }
    tx_errors_ = tx_errors;
    uint8_t data[64];
    bool lost = false;
    const uint16_t size = bluetooth_.Read(data, sizeof(data), lost);
    if (lost)
    {
        protocol_.DiscardUntilNewline();
        if (configured_)
        {
            FailTrial("uart_rx_lost");
        }
    }
    const uint32_t dropped = protocol_.GetStats().tx_dropped;
    protocol_.Feed(data, size);
    if (configured_ && protocol_.GetStats().tx_dropped != dropped)
    {
        FailTrial("ack_tx_lost");
    }
    CheckTrial();
    // One channel per 20 ms: 10 Hz per channel, under the 115200 UART bandwidth.
    // RX/ACK takes priority. A congested queue drops whole telemetry frames, never waits.
    if ((uint32_t)(now - last_publish_) >= 20U)
    {
        last_publish_ = now;
        if (M1TuningEnabled)
        {
            Publish(1); // 50 Hz for M1: leave UART/BLE capacity for ACK and heartbeat.
        }
        else
        {
            Publish(next_channel_);
            next_channel_ = next_channel_ == 5 ? 1 : next_channel_ + 1U;
        }
    }
}

void NovaLinkApp::Publish(uint8_t channel)
{
    const uint8_t index = channel - 1U;
    float target = 0, actual = 0, output = 0;
    uint32_t sample = 0;
    bool valid = false;
    const bool motor_fault = MotorSafety.GetStatus().fault != MotorProtection::Fault::None;
    if (channel <= 4)
    {
        DcMotor &motor = *motors[index];
        sample = motor.GetMeasure().timestamp_ms;
        target = motor.GetTargetSpeed();
        actual = motor.GetFilteredRpm();
        output = motor.GetDuty();
        valid = motor.IsOnline() && !motor_fault && motor.GetFault() == DcMotor::Fault::None;
    }
    else
    {
        if (!LineFollowBoard::Enabled)
        {
            return;
        }
        const auto &command = LineFollow.GetTrackCommand();
        // e = sp - pv = line_error, matching the existing steering sign convention.
        actual = -command.line_error;
        output = command.speed_diff_rpm;
        sample = command.timestamp_ms;
        valid = !motor_fault && command.valid && command.state == Track::State::Following &&
                (uint32_t)(HAL_GetTick() - sample) <= 50U;
        if (!valid && !motor_fault && System.GetState() != Systems::WORKING)
        {
            return;
        }
    }
    if (!valid)
    {
        if (!fault_sent_[index])
        {
            fault_sent_[index] =
                protocol_.Fault(channel, motor_fault ? "motor_protection" : "sample_unavailable");
        }
        return;
    }
    fault_sent_[index] = false;
    if (have_sample_[index] && samples_[index] == sample)
    {
        return;
    }
    have_sample_[index] = true;
    samples_[index] = sample;
    const uint32_t age = last_tick_ - sample;
    const double seconds = (double)(uptime_ms_ >= age ? uptime_ms_ - age : 0) * 0.001;
    if (!protocol_.Telemetry(channel, sequences_[index]++, seconds, target, actual, output) &&
        configured_)
    {
        FailTrial("telemetry_tx_lost");
    }
}

const NovaProtocol &NovaLinkApp::GetProtocol() const
{
    return protocol_;
}

const Bluetooth &NovaLinkApp::GetBluetooth() const
{
    return bluetooth_;
}
