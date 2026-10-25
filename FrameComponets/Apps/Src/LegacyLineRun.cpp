#include "LegacyLineRun.hpp"
#include "stm32f1xx_hal.h"
#include <math.h>
#include <stdio.h>

LegacyLineRun::LegacyLineRun(GraySensor &gray, Ultrasonic &range, LineFollowApp &line, DcMotor &m1,
                             DcMotor &m2, DcMotor &m3, DcMotor &m4, MotorProtection &protection)
    : gray_(gray), range_(range), line_(line), motors_{&m1, &m2, &m3, &m4}, protection_(protection)
{
}

LineFollowApp::Config LegacyLineRun::Configuration()
{
    LineFollowApp::Config config;
    config.maximum_rpm = TargetRpm;
    config.track.base_rpm = TargetRpm;
    config.track.turn_rpm = TargetRpm * 0.6f;
    config.track.kp = DefaultKp;
    config.track.ki = 0;
    config.track.kd = DefaultKd;
    config.track.maximum_diff_rpm = TargetRpm * 0.5f;
    config.track.white_line = true;
    config.track.hold_last_on_gap = true;
    // 60 mm blank allowance at 80% speed, plus 20 ms reacquisition; loss stays 1200 ms.
    config.track.gap_hold_ms = (uint32_t)fminf(
        300, fmaxf(120, ceilf(60000.0f * 60.0f / (3.14159265f * 64.0f * TargetRpm * 0.8f)) + 20));
    config.split_steering_slew = true;
    config.steering_slew_rpm_s = 240;
    return config;
}

void LegacyLineRun::DisableAll()
{
    for (auto motor : motors_)
    {
        motor->Disable();
    }
}

void LegacyLineRun::Fail(Failure failure, uint8_t port)
{
    if (status_.phase != Phase::Fault)
    {
        status_.failure = failure;
        status_.port = port;
    }
    status_.phase = Phase::Fault;
    gate_.Reset();
    status_.hold_ms = 0;
    line_.StopMotors();
    DisableAll();
    System.Stop(true);
}

bool LegacyLineRun::Init(void (*confirm_calibration)(bool))
{
    if (attempted_ || status_.phase == Phase::Stopped || status_.phase == Phase::Fault)
    {
        return false; // Reset/re-init never silently rearms a finished or faulted test.
    }
    attempted_ = true;
    line_.StopMotors();
    DisableAll();
    if (!confirm_calibration || !gray_.IsReady() || !range_.IsReady() ||
        protection_.GetStatus().fault != MotorProtection::Fault::None)
    {
        Fail(Failure::Initialization);
        return false;
    }
    // The module stores its own KEY1 calibration; this board profile uses WHITE=1.
    // Actual fresh eight-channel samples and a single white segment are still mandatory.
    confirm_calibration(true);
    for (uint8_t i = 0; i < 4; ++i)
    {
        auto &motor = *motors_[i];
        if (!motor.IsReady() || motor.GetMode() != DcMotor::Mode::Speed)
        {
            Fail(Failure::Initialization, i + 1);
            return false;
        }
        auto pid = motor.GetSpeedPidConfig();
        pid.kp = MotorKp;
        pid.ki = MotorKi;
        pid.kd = 0;
        pid.derivative_on_measurement = true;
        pid.integral_limit = DutyLimit;
        if (!motor.ConfigureSpeedPid(pid) || !motor.DutyLimSet(DutyLimit))
        {
            Fail(Failure::Initialization, i + 1);
            return false;
        }
    }
    display_.Init(); // Diagnostic-only failure; only this display is updated in this mode.
    boot_ms_ = system_ms_ = HAL_GetTick();
    status_.phase = Phase::Preparing;
    return true;
}

bool LegacyLineRun::HasStartLine() const
{
    const uint32_t now = HAL_GetTick();
    if (!gray_.HasFreshSample(now, 50) || gray_.GetSample().channels != 8)
    {
        return false;
    }
    const auto &sample = gray_.GetSample();
    unsigned active = 0, groups = 0;
    bool previous = false;
    for (unsigned i = 0; i < 8; ++i)
    {
        if (!isfinite(sample.line[i]) || sample.line[i] < 0 || sample.line[i] > 1)
        {
            return false;
        }
        const bool on = sample.line[i] >= 0.5f;
        active += on;
        groups += on && !previous;
        previous = on;
    }
    return active > 0 && active < 7 && groups == 1;
}

bool LegacyLineRun::CheckFeedback()
{
    for (uint8_t i = 0; i < 4; ++i)
    {
        auto &motor = *motors_[i];
        if (!motor.IsOnline())
        {
            if (!ever_enabled_ && (uint32_t)(HAL_GetTick() - boot_ms_) < 100U)
            {
                continue;
            }
            Fail(Failure::Feedback, i + 1);
            return false;
        }
        const float raw = motor.GetMeasure().speed_rpm, filtered = motor.GetFilteredRpm();
        if (!isfinite(raw) || !isfinite(filtered) || raw < -120 || raw > 216 || filtered < -120 ||
            filtered > 216)
        {
            Fail(Failure::FeedbackRange, i + 1);
            return false;
        }
        const float target = motor.GetTargetSpeed();
        if (!isfinite(target) || target < 0 || target > TargetRpm)
        {
            Fail(Failure::TargetRange, i + 1);
            return false;
        }
        if (motor.IsEnabled() && fabsf(motor.GetDuty()) >= DutyLimit * 0.999f)
        {
            if (!saturated_[i])
            {
                saturated_[i] = true;
                saturated_ms_[i] = HAL_GetTick();
            }
            else if ((uint32_t)(HAL_GetTick() - saturated_ms_[i]) >= 1000U)
            {
                Fail(Failure::Saturation, i + 1);
                return false;
            }
        }
        else
        {
            saturated_[i] = false;
        }
    }
    return true;
}

void LegacyLineRun::Run()
{
    if (status_.phase == Phase::Disarmed || status_.phase == Phase::Fault ||
        status_.phase == Phase::Stopped)
    {
        DisableAll();
        UpdateDisplay();
        return;
    }
    const uint32_t now = HAL_GetTick();
    range_.Update();
    gray_.Update();
    status_.gray_mask = 0;
    if (gray_.HasFreshSample(now, 50) && gray_.GetSample().channels == 8)
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            status_.gray_mask |= (gray_.GetSample().line[i] >= 0.5f ? 1U : 0U) << i;
        }
    }
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (status_.phase != Phase::Running && motors_[i]->IsEnabled())
        {
            Fail(Failure::UnexpectedEnable, i + 1);
            return;
        }
        if (ever_enabled_ && !motors_[i]->IsEnabled())
        {
            Fail(Failure::UnexpectedDisable, i + 1);
            return;
        }
    }
    if (!protection_.BeforeControl())
    {
        Fail(Failure::Protection, protection_.GetStatus().failed_port);
        return;
    }
    DcMotor::ControlAllMotors(); // Includes fresh encoders while disabled, before the first start.
    if (!protection_.AfterControl())
    {
        Fail(Failure::Protection, protection_.GetStatus().failed_port);
        return;
    }
    if (!CheckFeedback())
    {
        return;
    }
    if ((uint32_t)(now - system_ms_) >= FRAME_SYSTEM_PERIOD_MS)
    {
        system_ms_ = now;
        System.UpdateApplications();
        System.Run();
    }
    if (System.GetState() == Systems::ERROR ||
        (System.GetState() == Systems::STOP && status_.phase != Phase::Stopped))
    {
        Fail(Failure::SystemState);
        return;
    }
    const bool prerequisites = HasStartLine() && System.GetState() == Systems::READY;
    if (status_.phase == Phase::Preparing && prerequisites)
    {
        if (!protection_.IsCoasting() && !protection_.RequestCoastStop())
        {
            Fail(Failure::Start);
            return;
        }
        status_.phase = Phase::Waiting;
    }
    if (status_.phase != Phase::Running)
    {
        DisableAll();
        const bool ready =
            status_.phase == Phase::Waiting && prerequisites && protection_.CanResumeAfterCoast();
        const bool start =
            gate_.Update(range_.GetSample(), now, ready, StartHoldMs, StartDistanceMm);
        if (start)
        {
            if (!protection_.ReleaseCoastStop() || !line_.RequestStart())
            {
                Fail(Failure::Start);
                return;
            }
            System.Run();
            if (System.GetState() != Systems::WORKING)
            {
                Fail(Failure::Start);
                return;
            }
            status_.phase = Phase::Running;
            started_ms_ = now;
        }
        status_.hold_ms = gate_.ElapsedMs();
    }
    if (status_.phase == Phase::Running)
    {
        line_.Control();
        if (System.GetState() != Systems::WORKING)
        {
            status_.track_state =
                System.GetState() == Systems::STOP ? Track::State::Lost : Track::State::Invalid;
            Fail(Failure::LostLine);
            return;
        }
        status_.track_state = line_.GetTrackCommand().state;
        status_.line_error = line_.GetTrackCommand().line_error;
        bool enabled = false;
        for (auto motor : motors_)
        {
            enabled = enabled || motor->IsEnabled();
        }
        ever_enabled_ = ever_enabled_ || enabled;
        if (!ever_enabled_ && (uint32_t)(now - started_ms_) >= 1000U)
        {
            Fail(Failure::Start);
            return;
        }
    }
    UpdateDisplay();
}

void LegacyLineRun::Stop()
{
    if (status_.phase != Phase::Fault)
    {
        status_.phase = Phase::Stopped;
    }
    gate_.Reset();
    status_.hold_ms = 0;
    line_.StopMotors();
    DisableAll();
    System.Stop(status_.phase == Phase::Fault);
}

const LegacyLineRun::Status &LegacyLineRun::GetStatus() const
{
    return status_;
}

void LegacyLineRun::UpdateDisplay()
{
    display_.Update();
    const uint32_t now = HAL_GetTick();
    if (display_.IsBusy() || (uint32_t)(now - display_ms_) < 100U)
    {
        return;
    }
    display_ms_ = now;
    char text[22];
    if (status_.phase == Phase::Warmup)
    {
        const uint32_t elapsed = now - boot_ms_;
        const uint32_t seconds = elapsed < 20000U ? (20000U - elapsed + 999U) / 1000U : 0;
        snprintf(text, sizeof(text), "GRAY WARM %luS", (unsigned long)seconds);
    }
    else if (status_.phase == Phase::Fault)
    {
        snprintf(text, sizeof(text), "FAULT %u M%u", (unsigned)status_.failure,
                 (unsigned)status_.port);
    }
    else if (status_.phase == Phase::Running)
    {
        snprintf(text, sizeof(text), "LINE %u STATE %u", (unsigned)TargetRpm,
                 (unsigned)status_.track_state);
    }
    else if (status_.phase == Phase::Stopped)
    {
        snprintf(text, sizeof(text), "STOPPED RESET TO ARM");
    }
    else if (status_.phase == Phase::Preparing)
    {
        snprintf(text, sizeof(text), "WAIT VALID WHITE LINE");
    }
    else
    {
        snprintf(
            text, sizeof(text), "NEAR %lu.%lu OF %luS", (unsigned long)(status_.hold_ms / 1000U),
            (unsigned long)((status_.hold_ms / 100U) % 10U), (unsigned long)(StartHoldMs / 1000U));
    }
    display_.SetLine(0, text);
    if (range_.HasFreshSample(now, 100))
    {
        const unsigned mm = (unsigned)fminf(range_.GetSample().distance_mm, 9999.0f);
        snprintf(text, sizeof(text), "US %u.%u CM", mm / 10, mm % 10);
    }
    else
    {
        snprintf(text, sizeof(text), "US ---");
    }
    display_.SetLine(1, text);
    char bits[] = "X 00000000";
    for (unsigned i = 0; i < 8; ++i)
    {
        bits[2 + i] = gray_.HasFreshSample(now, 50) && gray_.GetSample().channels == 8
                          ? (gray_.GetSample().line[i] >= 0.5f ? '1' : '0')
                          : '-';
    }
    display_.SetLine(2, bits);
    snprintf(text, sizeof(text), "SP L%03u R%03u",
             (unsigned)fminf(fmaxf(motors_[0]->GetTargetSpeed(), 0), 180),
             (unsigned)fminf(fmaxf(motors_[2]->GetTargetSpeed(), 0), 180));
    display_.SetLine(3, text);
    display_.Refresh();
}
