#pragma once

#include "gray_sensor.hpp"
#include "pid.hpp"

// APP policy only: samples are calibrated WHITE=1, ordered vehicle left to right.
class Track
{
public:
    enum class PidMode
    {
        Straight,
        Turn
    };
    enum class State
    {
        Disabled,
        WaitingForLine,
        Following,
        GapHold,
        Searching,
        Lost,
        Invalid
    };

    struct Config
    {
        float base_rpm = 40;
        float turn_rpm = 28;
        float gap_rpm = 24;
        float search_rpm = 20;
        float search_diff_rpm = 18;
        float maximum_diff_rpm = 24;
        float curve_boost_rpm = 0; // Opt-in outer-error steering, zero preserves legacy PD.
        float curve_speed_drop_rpm = 0; // Extra base reduction at full outer error.
        float curve_start_error = 0.4f; // Normalized absolute error; central PD stays unchanged.
        bool curve_linear_ramp = false; // Earlier outer steering; legacy curve remains quadratic.
        float kp = 28; // RPM per normalized line error.
        float kd = 0.25f; // RPM seconds per normalized line error.
        float ki = 0; // Default behavior remains PD; integral output is bounded in RPM.
        float integral_limit_rpm = 6;
        float error_filter_tau_s = 0.025f;
        bool white_line = true;
        bool dual_pid = false;
        float turn_kp = 28, turn_ki = 0, turn_kd = .25f;
        float turn_base_limit_rpm = 90;
        bool hold_last_on_gap = false; // Explicit APP policy for bounded blank dashed segments.
        uint8_t reacquire_samples = 3;
        uint32_t sample_timeout_ms = 50;
        uint32_t gap_hold_ms = 120;
        uint32_t loss_timeout_ms = 1200;
    };

    struct Command
    {
        float base_rpm = 0;
        float speed_diff_rpm = 0; // Positive: left faster, steer right.
        float line_error = 0; // Left negative, right positive, normalized [-1,1].
        uint32_t timestamp_ms = 0; // Last actual line sample, never refreshed by telemetry.
        State state = State::Disabled;
        bool valid = false;
    };

    bool Init(const Config &config);
    void Reset();
    const Command &Update(const GraySensor::Sample &sample, uint32_t now_ms);
    const Command &GetCommand() const;

    PidMode GetPidMode() const
    {
        return mode_;
    }

    bool SetCruiseRpm(float rpm);

private:
    bool FindLine(const GraySensor::Sample &sample, float &error) const;
    void HandleMissingLine(uint32_t now_ms);
    void Follow(float error, uint32_t sample_ms);
    void SelectPid(float error, uint32_t sample_ms, bool restarting);

    Config config_;
    Command command_;
    PidGeneral pid_;
    PidGeneral turn_pid_;
    PidMode mode_ = PidMode::Straight;
    float turn_direction_ = 0;
    uint32_t enter_ms_ = 0, exit_ms_ = 0;
    bool entering_ = false, leaving_ = false, opposite_exit_ = false;
    uint32_t last_sample_ms_ = 0;
    uint32_t last_line_ms_ = 0;
    uint32_t filter_ms_ = 0;
    float filtered_error_ = 0;
    float last_diff_rpm_ = 0;
    float last_base_rpm_ = 0;
    float last_direction_ = 0;
    uint8_t reacquire_count_ = 0;
    bool initialized_ = false;
    bool sampled_ = false;
    bool seen_line_ = false;
    bool filter_valid_ = false;
    bool gap_hold_allowed_ = false;
};
