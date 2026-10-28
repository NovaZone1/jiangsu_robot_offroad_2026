#include "Track.hpp"
#include <math.h>

namespace
{
    bool Nonnegative(float value)
    {
        return isfinite(value) && value >= 0;
    }
} // namespace

bool Track::Init(const Config &config)
{
    initialized_ = false;
    Reset();
    if (!Nonnegative(config.base_rpm) || !Nonnegative(config.turn_rpm) ||
        !Nonnegative(config.gap_rpm) || !Nonnegative(config.search_rpm) ||
        !Nonnegative(config.search_diff_rpm) || !Nonnegative(config.maximum_diff_rpm) ||
        !Nonnegative(config.curve_boost_rpm) || config.curve_boost_rpm > config.maximum_diff_rpm ||
        !Nonnegative(config.curve_speed_drop_rpm) ||
        config.curve_speed_drop_rpm > config.turn_rpm || !Nonnegative(config.curve_start_error) ||
        config.curve_start_error >= 1 || !Nonnegative(config.kp) || !Nonnegative(config.kd) ||
        !Nonnegative(config.ki) || !Nonnegative(config.integral_limit_rpm) ||
        config.integral_limit_rpm == 0 ||
        (config.ki > 0 && config.integral_limit_rpm > config.maximum_diff_rpm) ||
        !Nonnegative(config.error_filter_tau_s) || config.base_rpm <= 0 ||
        config.turn_rpm > config.base_rpm || config.gap_rpm > config.base_rpm ||
        config.search_rpm > config.base_rpm || config.search_diff_rpm > config.search_rpm ||
        config.maximum_diff_rpm > config.base_rpm || config.reacquire_samples == 0 ||
        (config.dual_pid &&
         (!Nonnegative(config.turn_kp) || !Nonnegative(config.turn_ki) ||
          !Nonnegative(config.turn_kd) || !Nonnegative(config.turn_base_limit_rpm) ||
          config.turn_base_limit_rpm <= 0 || config.turn_base_limit_rpm > config.base_rpm)) ||
        config.sample_timeout_ms == 0 || config.sample_timeout_ms >= 0x80000000U ||
        config.gap_hold_ms >= config.loss_timeout_ms || config.loss_timeout_ms >= 0x80000000U)
    {
        return false;
    }
    config_ = config;
    pid_.Init(config.kp, config.ki, config.kd);
    pid_.SetLimit(config.integral_limit_rpm, config.maximum_diff_rpm, 0.2f);
    turn_pid_.Init(config.turn_kp, config.turn_ki, config.turn_kd);
    turn_pid_.SetLimit(config.integral_limit_rpm, config.maximum_diff_rpm, 0.2f);
    initialized_ = true;
    Reset();
    return true;
}

void Track::Reset()
{
    command_ = {};
    pid_.Reset();
    turn_pid_.Reset();
    mode_ = PidMode::Straight;
    entering_ = leaving_ = false;
    turn_direction_ = 0;
    last_sample_ms_ = last_line_ms_ = filter_ms_ = 0;
    filtered_error_ = last_diff_rpm_ = last_direction_ = last_base_rpm_ = 0;
    reacquire_count_ = 0;
    sampled_ = seen_line_ = filter_valid_ = false;
    gap_hold_allowed_ = false;
}

bool Track::FindLine(const GraySensor::Sample &sample, float &error) const
{
    float weight = 0;
    float moment = 0;
    uint8_t active = 0;
    uint8_t groups = 0;
    bool previous = false;
    for (uint8_t index = 0; index < sample.channels; ++index)
    {
        const float value = config_.white_line ? sample.line[index] : 1.0f - sample.line[index];
        const bool on_line = value >= 0.5f;
        if (on_line)
        {
            ++active;
            if (!previous)
            {
                ++groups;
            }
            weight += value;
            moment += value * (2.0f * index / (sample.channels - 1U) - 1.0f);
        }
        previous = on_line;
    }
    // Wide marks and multiple separated lines require a route policy, not a fake centroid.
    if (active == 0 || active >= sample.channels - 1U || groups != 1)
    {
        return false;
    }
    error = moment / weight;
    return true;
}

void Track::HandleMissingLine(uint32_t now_ms)
{
    mode_ = PidMode::Straight;
    entering_ = leaving_ = false;
    command_.valid = false;
    command_.base_rpm = command_.speed_diff_rpm = 0;
    if (!seen_line_)
    {
        command_.state = State::WaitingForLine;
        return;
    }
    const uint32_t missing_ms = now_ms - last_line_ms_;
    if (missing_ms >= config_.loss_timeout_ms)
    {
        command_.state = State::Lost;
        return;
    }
    command_.valid = true;
    if (missing_ms <= config_.gap_hold_ms)
    {
        command_.state = State::GapHold;
        command_.base_rpm = gap_hold_allowed_ ? last_base_rpm_ : config_.gap_rpm;
        command_.speed_diff_rpm = last_diff_rpm_;
    }
    else
    {
        command_.state = State::Searching;
        command_.base_rpm = config_.search_rpm;
        // Alternate widening arcs if the last reliable line was centered.
        const bool alternate = ((missing_ms - config_.gap_hold_ms) / 250U) % 2U != 0U;
        const float direction = last_direction_ != 0 ? last_direction_ : (alternate ? -1.0f : 1.0f);
        command_.speed_diff_rpm = direction * config_.search_diff_rpm;
    }
}

void Track::Follow(float error, uint32_t sample_ms)
{
    const bool restarting = !filter_valid_ || command_.state != State::Following;
    const float dt = restarting ? 0.005f : (float)(sample_ms - filter_ms_) * 0.001f;
    if (restarting)
    {
        pid_.Reset();
        filtered_error_ = error;
    }
    else
    {
        const float alpha = dt / (config_.error_filter_tau_s + dt);
        filtered_error_ += alpha * (error - filtered_error_);
    }
    filter_ms_ = sample_ms;
    filter_valid_ = true;
    SelectPid(filtered_error_, sample_ms, restarting);
    auto &controller = config_.dual_pid && mode_ == PidMode::Turn ? turn_pid_ : pid_;
    // Calc(error, 0) gives a positive difference for a line on the right.
    if (restarting)
    {
        controller.kd_error = controller.last_kd_error = filtered_error_;
    }
    controller.ManualDt(dt);
    command_.speed_diff_rpm = controller.Calc(filtered_error_, 0, config_.maximum_diff_rpm);
    command_.base_rpm =
        config_.base_rpm - (config_.base_rpm - config_.turn_rpm) * fabsf(filtered_error_);
    if (config_.dual_pid)
    {
        command_.base_rpm =
            fminf(command_.base_rpm, config_.base_rpm - fabsf(command_.speed_diff_rpm));
        if (mode_ == PidMode::Turn)
        {
            command_.base_rpm = fminf(command_.base_rpm, config_.turn_base_limit_rpm);
        }
    }
    if (!config_.dual_pid && fabsf(filtered_error_) > config_.curve_start_error)
    {
        // Preserve the actual centroid and center response; ramp steering only near the edges.
        const float fraction = fminf(1, (fabsf(filtered_error_) - config_.curve_start_error) /
                                            (1 - config_.curve_start_error));
        const float amount = config_.curve_linear_ramp ? fraction : fraction * fraction;
        const float boost = config_.curve_boost_rpm * amount * (filtered_error_ > 0 ? 1 : -1);
        command_.speed_diff_rpm =
            fmaxf(-config_.maximum_diff_rpm,
                  fminf(config_.maximum_diff_rpm, command_.speed_diff_rpm + boost));
        command_.base_rpm -= config_.curve_speed_drop_rpm * amount;
    }
    command_.line_error = filtered_error_;
    command_.timestamp_ms = sample_ms;
    command_.state = State::Following;
    command_.valid = true;
    last_diff_rpm_ = command_.speed_diff_rpm;
    if (fabsf(filtered_error_) > 0.05f)
    {
        last_direction_ = filtered_error_ > 0 ? 1.0f : -1.0f;
    }
}

const Track::Command &Track::Update(const GraySensor::Sample &sample, uint32_t now_ms)
{
    if (!initialized_)
    {
        command_ = {};
        command_.state = State::Invalid;
        return command_;
    }
    // Lost is latched until Reset; an old command never restarts the vehicle.
    if (command_.state == State::Lost || command_.state == State::Invalid)
    {
        return command_;
    }
    bool valid = sample.valid && sample.channels >= 4 &&
                 sample.channels <= FRAME_GRAY_MAX_CHANNELS &&
                 (uint32_t)(now_ms - sample.timestamp_ms) <= config_.sample_timeout_ms;
    for (uint8_t index = 0; valid && index < sample.channels; ++index)
    {
        valid = isfinite(sample.line[index]) && sample.line[index] >= 0 && sample.line[index] <= 1;
    }
    if (!valid)
    {
        command_ = {};
        command_.state = State::Invalid;
        return command_;
    }
    if (sampled_ && sample.timestamp_ms == last_sample_ms_)
    {
        if (command_.state != State::Following)
        {
            HandleMissingLine(now_ms);
        }
        return command_;
    }
    sampled_ = true;
    last_sample_ms_ = sample.timestamp_ms;
    float error = 0;
    if (!FindLine(sample, error))
    {
        bool blank = true;
        for (uint8_t i = 0; i < sample.channels; ++i)
        {
            const float value = config_.white_line ? sample.line[i] : 1.0f - sample.line[i];
            blank = blank && value < 0.5f;
        }
        if (command_.state == State::Following)
        {
            gap_hold_allowed_ = config_.hold_last_on_gap && blank;
        }
        if (!blank)
        {
            gap_hold_allowed_ = false; // Wide/separated marks never get high-speed blank hold.
        }
        reacquire_count_ = 0;
        HandleMissingLine(now_ms);
        return command_;
    }
    // Check the deadline before accepting a late reacquisition.
    if (seen_line_ && (uint32_t)(now_ms - last_line_ms_) >= config_.loss_timeout_ms)
    {
        HandleMissingLine(now_ms);
        return command_;
    }
    if (command_.state != State::Following && ++reacquire_count_ < config_.reacquire_samples)
    {
        HandleMissingLine(now_ms);
        return command_;
    }
    seen_line_ = true;
    last_line_ms_ = sample.timestamp_ms;
    reacquire_count_ = 0;
    Follow(error, sample.timestamp_ms);
    last_base_rpm_ = command_.base_rpm;
    gap_hold_allowed_ = false;
    return command_;
}

const Track::Command &Track::GetCommand() const
{
    return command_;
}

bool Track::SetCruiseRpm(float rpm)
{
    if (!initialized_ || !config_.dual_pid || !isfinite(rpm) || rpm < 120 || rpm > 180)
    {
        return false;
    }
    config_.base_rpm = rpm;
    return true;
}

void Track::SelectPid(float error, uint32_t now, bool restarting)
{
    if (!config_.dual_pid)
    {
        return;
    }
    if (restarting)
    {
        entering_ = leaving_ = false;
        mode_ = PidMode::Straight;
    }
    const auto previous = mode_;
    if (mode_ == PidMode::Straight)
    {
        const float direction = fabsf(error) >= .25f ? (error > 0 ? 1.f : -1.f) : 0;
        if (!direction)
        {
            entering_ = false;
        }
        else
        {
            if (!entering_ || direction != turn_direction_)
            {
                enter_ms_ = now;
            }
            entering_ = true;
            turn_direction_ = direction;
            if ((uint32_t)(now - enter_ms_) >= 10)
            {
                mode_ = PidMode::Turn;
                leaving_ = false;
            }
        }
    }
    else
    {
        const bool centered = fabsf(error) <= .16f;
        const bool opposite = error * turn_direction_ <= -.25f;
        if (!centered && !opposite)
        {
            leaving_ = false;
        }
        else
        {
            if (!leaving_ || opposite_exit_ != opposite)
            {
                exit_ms_ = now;
            }
            opposite_exit_ = opposite;
            leaving_ = true;
            if ((uint32_t)(now - exit_ms_) >= (opposite ? 25U : 300U))
            {
                mode_ = PidMode::Straight;
                entering_ = leaving_ = false;
            }
        }
    }
    if (mode_ != previous || restarting)
    {
        auto &controller = mode_ == PidMode::Turn ? turn_pid_ : pid_;
        controller.Reset();
        controller.kd_error = controller.last_kd_error = error;
    }
}
