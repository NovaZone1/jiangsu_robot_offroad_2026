#include "LineFollowApp.hpp"
#include "stm32f1xx_hal.h"
#include <math.h>

LineFollowApp::LineFollowApp(GraySensor &gray, DcMotor &m1, DcMotor &m2, DcMotor &m3, DcMotor &m4)
    : Application("LineFollow"), gray_(gray), motors_{&m1, &m2, &m3, &m4}, display_(m1, m2, m3, m4)
{
}

bool LineFollowApp::Init(const Config &config)
{
    StopMotors();
    initialized_ = false;
    if (!isfinite(config.acceleration_rpm_s) || config.acceleration_rpm_s <= 0 ||
        !isfinite(config.deceleration_rpm_s) || config.deceleration_rpm_s <= 0 ||
        !isfinite(config.steering_slew_rpm_s) || config.steering_slew_rpm_s <= 0 ||
        !isfinite(config.maximum_rpm) || config.track.base_rpm > config.maximum_rpm ||
        (config.course_run &&
         (!config.track.dual_pid || config.maximum_rpm != 180 || !config.allow_180_rpm)) ||
        !track_.Init(config.track) || !mixer_.Init(config.maximum_rpm, 50, config.allow_180_rpm))
    {
        return false;
    }
    for (uint8_t index = 0; index < 4; ++index)
    {
        if (!motors_[index]->IsReady() || motors_[index]->GetMode() != DcMotor::Mode::Speed)
        {
            return false;
        }
        for (uint8_t previous = 0; previous < index; ++previous)
        {
            if (motors_[index] == motors_[previous])
            {
                return false;
            }
        }
    }
    config_ = config;
    // Missing OLED is diagnostic-only; it cannot block acquisition or motor stop.
    display_.Init();
    initialized_ = true;
    return true;
}

bool LineFollowApp::WatchPoint()
{
    if (!initialized_ || !gray_.IsReady() ||
        !gray_.HasFreshSample(HAL_GetTick(), config_.track.sample_timeout_ms))
    {
        return false;
    }
    for (DcMotor *motor : motors_)
    {
        if (!motor->IsReady() || !motor->IsOnline() || motor->GetMode() != DcMotor::Mode::Speed)
        {
            return false;
        }
    }
    return true;
}

App::Status LineFollowApp::GetStatus()
{
    if (System.GetState() == Systems::WORKING && !WatchPoint())
    {
        return App::Error;
    }
    return WatchPoint() ? App::Normal : App::Warning;
}

bool LineFollowApp::RequestStart()
{
    if (System.GetState() != Systems::READY || !WatchPoint())
    {
        return false;
    }
    System.system_start_to_work_flag = true;
    return true;
}

void LineFollowApp::Start()
{
    StopMotors();
}

void LineFollowApp::Update()
{
    // GetStatus participates in the existing self-check/health lifecycle.
    // Control is explicitly called at the base tick, never from another task.
}

void LineFollowApp::StopMotors()
{
    for (uint8_t index = 0; index < 4; ++index)
    {
        motors_[index]->Disable();
        submitted_rpm_[index] = 0;
    }
    track_.Reset();
    mixer_.ClearAll();
    running_ = false;
    submitted_base_rpm_ = submitted_diff_rpm_ = 0;
}

bool LineFollowApp::ApplySpeeds(const SpeedMixer::WheelSpeeds &speeds, float dt)
{
    const bool clearing =
        config_.course_run && finish_.GetStatus().phase == FinishLineGate::Phase::Clearing;
    const float ceiling = clearing ? 50.f
                          : config_.course_run && track_.GetPidMode() == Track::PidMode::Turn
                              ? 120.f
                              : config_.maximum_rpm;
    bool all_enabled = true;
    bool any_enabled = false;
    for (DcMotor *motor : motors_)
    {
        all_enabled = all_enabled && motor->IsEnabled();
        any_enabled = any_enabled || motor->IsEnabled();
    }
    // Unexpected partial disable is a fault, not a reason to silently restart one wheel.
    if (any_enabled && !all_enabled)
    {
        return false;
    }
    if (config_.split_steering_slew)
    {
        const float desired_base = (speeds.rpm[0] + speeds.rpm[2]) * 0.5f;
        const float desired_diff = (speeds.rpm[0] - speeds.rpm[2]) * 0.5f;
        const float base_change = desired_base - submitted_base_rpm_;
        const float base_limit =
            (base_change >= 0 ? config_.acceleration_rpm_s : config_.deceleration_rpm_s) * dt;
        submitted_base_rpm_ += fmaxf(-base_limit, fminf(base_change, base_limit));
        if (config_.course_run)
        {
            submitted_base_rpm_ = fminf(ceiling, submitted_base_rpm_);
        }
        const float diff_limit = config_.steering_slew_rpm_s * dt;
        submitted_diff_rpm_ +=
            fmaxf(-diff_limit, fminf(desired_diff - submitted_diff_rpm_, diff_limit));
        const float headroom = fmaxf(0, fminf(submitted_base_rpm_, ceiling - submitted_base_rpm_));
        submitted_diff_rpm_ = fmaxf(-headroom, fminf(submitted_diff_rpm_, headroom));
    }
    for (uint8_t index = 0; index < 4; ++index)
    {
        if (!all_enabled && !motors_[index]->Enable())
        {
            return false;
        }
        if (config_.split_steering_slew)
        {
            submitted_rpm_[index] =
                submitted_base_rpm_ + (index < 2 ? submitted_diff_rpm_ : -submitted_diff_rpm_);
        }
        else
        {
            const float change = speeds.rpm[index] - submitted_rpm_[index];
            const float limit =
                (change >= 0 ? config_.acceleration_rpm_s : config_.deceleration_rpm_s) * dt;
            const float increment = change > limit ? limit : (change < -limit ? -limit : change);
            submitted_rpm_[index] += increment;
        }
        if (!motors_[index]->SetSpeed(submitted_rpm_[index]))
        {
            return false;
        }
    }
    return true;
}

void LineFollowApp::Control()
{
    if (!initialized_)
    {
        return;
    }
    if (System.GetState() != Systems::WORKING)
    {
        if (running_)
        {
            StopMotors();
        }
        return;
    }
    const uint32_t now = HAL_GetTick();
    if (!WatchPoint())
    {
        StopMotors();
        System.Stop(true);
        return;
    }
    if (!running_)
    {
        track_.Reset();
        mixer_.ClearAll();
        submitted_base_rpm_ = submitted_diff_rpm_ = 0;
        running_ = true;
        last_control_ms_ = now - FRAME_SYSTEM_PERIOD_MS;
        if (config_.course_run)
        {
            course_.Reset(now);
            finish_.Reset();
            for (unsigned i = 0; i < 4; ++i)
            {
                distance_ms_[i] = motors_[i]->GetMeasure().timestamp_ms;
                wheel_distance_m_[i] = 0;
            }
        }
    }
    const uint32_t elapsed = now - last_control_ms_;
    if (elapsed < FRAME_SYSTEM_PERIOD_MS)
    {
        return;
    }
    // A scheduling stall must stop instead of applying a large slew step.
    if (elapsed >= 50U)
    {
        StopMotors();
        System.Stop(true);
        return;
    }
    last_control_ms_ = now;
    if (config_.course_run && !track_.SetCruiseRpm(course_.GetStatus().cruise_rpm))
    {
        StopMotors();
        System.Stop(true);
        return;
    }
    const Track::Command &command = track_.Update(gray_.GetSample(), now);
    if (config_.course_run && !UpdateCourse(now))
    {
        StopMotors();
        System.Stop(true);
        return;
    }
    if (command.state == Track::State::Lost || command.state == Track::State::Invalid)
    {
        const bool invalid = command.state == Track::State::Invalid;
        StopMotors();
        System.Stop(invalid);
        return;
    }
    if (!command.valid)
    {
        mixer_.ClearAll();
        submitted_base_rpm_ = submitted_diff_rpm_ = 0;
        for (uint8_t index = 0; index < 4; ++index)
        {
            motors_[index]->Disable();
            submitted_rpm_[index] = 0;
        }
        return;
    }
    if (config_.course_run && finish_.GetStatus().phase == FinishLineGate::Phase::Done)
    {
        return;
    }
    SpeedMixer::WheelSpeeds speeds;
    const bool clearing =
        config_.course_run && finish_.GetStatus().phase == FinishLineGate::Phase::Clearing;
    const float base = clearing ? fminf(40, command.base_rpm) : command.base_rpm;
    const float diff =
        clearing ? fmaxf(-10, fminf(10, command.speed_diff_rpm)) : command.speed_diff_rpm;
    if (!mixer_.SetTrackSpeed(base, diff, now) || !mixer_.GetWheelSpeeds(now, speeds) ||
        !ApplySpeeds(speeds, (float)elapsed * 0.001f))
    {
        StopMotors();
        System.Stop(true);
    }
}

bool LineFollowApp::UpdateCourse(uint32_t now)
{
    const auto &g = gray_.GetSample();
    if (!g.valid || g.channels != 8 || (uint32_t)(now - g.timestamp_ms) >= 50)
    {
        return false;
    }
    float distance = 0;
    bool tracking = true;
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto &m = motors_[i]->GetMeasure();
        if (!m.valid || !isfinite(m.speed_rpm) || (uint32_t)(now - m.timestamp_ms) >= 50)
        {
            return false;
        }
        const uint32_t dt = m.timestamp_ms - distance_ms_[i];
        if (dt >= 50)
        {
            return false;
        }
        if (dt)
        {
            wheel_distance_m_[i] += m.speed_rpm * ((float)dt / 60000) * .20106193f;
            distance_ms_[i] = m.timestamp_ms;
        }
        distance += wheel_distance_m_[i] * .25f;
        tracking =
            tracking && fabsf(motors_[i]->GetFilteredRpm() - motors_[i]->GetTargetSpeed()) <= 12;
    }
    const auto &c = track_.GetCommand();
    const bool reliable = c.valid && c.state == Track::State::Following;
    const bool straight = track_.GetPidMode() == Track::PidMode::Straight;
    const bool centered = reliable && fabsf(c.line_error) <= .15f;
    const auto &s =
        course_.Update(now, fmaxf(0, distance), centered, reliable, !straight, tracking);
    if (s.invalid)
    {
        return false;
    }
    uint8_t mask = 0;
    for (unsigned i = 0; i < 8; ++i)
    {
        if (g.line[i] >= .5f)
        {
            mask |= 1U << i;
        }
    }
    const auto previous = finish_.GetStatus().crossings;
    const auto &f = finish_.Update(g.timestamp_ms, now, mask, reliable, straight, distance,
                                   wheel_distance_m_[0], wheel_distance_m_[2], s.completed_bends,
                                   s.leg_distance_m);
    if (f.invalid)
    {
        return false;
    }
    if (f.crossings != previous)
    {
        course_.NoteLowerMarker(f.crossings, distance);
    }
    return true;
}

void LineFollowApp::UpdateDisplay()
{
    display_.Update();
}

const Track::Command &LineFollowApp::GetTrackCommand() const
{
    return track_.GetCommand();
}

SpeedMixer &LineFollowApp::GetSpeedMixer()
{
    return mixer_;
}

const Track::Config &LineFollowApp::GetTrackConfig() const
{
    return config_.track;
}

bool LineFollowApp::ConfigureTrackPid(float kp, float ki, float kd)
{
    if (!initialized_ || running_ || System.GetState() == Systems::WORKING || !isfinite(kp) ||
        !isfinite(ki) || !isfinite(kd) || kp < 0 || kp > 100 || ki < 0 || ki > 100 || kd < 0 ||
        kd > 10)
    {
        return false;
    }
    for (DcMotor *motor : motors_)
    {
        if (motor->IsEnabled())
        {
            return false;
        }
    }
    Track::Config next = config_.track;
    next.kp = kp;
    next.ki = ki;
    next.kd = kd;
    if (!track_.Init(next))
    {
        return false;
    }
    config_.track = next;
    return true;
}
