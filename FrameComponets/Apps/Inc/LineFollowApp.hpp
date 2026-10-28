#pragma once

#include "System.hpp"
#include "Track.hpp"
#include "SpeedMixer.hpp"
#include "MotorSpeedDisplay.hpp"
#include "CoursePlanner.hpp"
#include "FinishLineGate.hpp"

// Application lifecycle and all four motor writes remain owned by the framework task.
class LineFollowApp : public Application
{
public:
    struct Config
    {
        Track::Config track;
        float maximum_rpm = 60;
        bool allow_180_rpm = false; // Explicit board opt-in; generic mixer guard stays 160.
        float acceleration_rpm_s = 120;
        float deceleration_rpm_s = 240;
        bool split_steering_slew = false;
        float steering_slew_rpm_s = 240; // Half left/right difference per second.
        bool course_run = false;
    };

    LineFollowApp(GraySensor &gray, DcMotor &m1, DcMotor &m2, DcMotor &m3, DcMotor &m4);
    bool Init(const Config &config);
    bool WatchPoint() override;
    App::Status GetStatus() override;
    bool RequestStart(); // Explicit request only; initialization never starts motion.
    void Control(); // Every 1 ms, computes and submits targets every 5 ms.
    void UpdateDisplay(); // Every 1 ms, measured RPM frames scheduled at 20 Hz.
    void StopMotors();
    const Track::Command &GetTrackCommand() const;
    SpeedMixer &GetSpeedMixer(); // Future APP policies share this single motor writer.
    const Track::Config &GetTrackConfig() const;

    const CoursePlanner::Status &GetCourseStatus() const
    {
        return course_.GetStatus();
    }

    const FinishLineGate::Status &GetFinishStatus() const
    {
        return finish_.GetStatus();
    }

    Track::PidMode GetPidMode() const
    {
        return track_.GetPidMode();
    }

    bool ConfigureTrackPid(float kp, float ki, float kd); // Stopped and all motors disabled only.

protected:
    void Start() override;
    void Update() override;

private:
    bool ApplySpeeds(const SpeedMixer::WheelSpeeds &speeds, float dt);
    GraySensor &gray_;
    DcMotor *motors_[4];
    Track track_;
    SpeedMixer mixer_;
    MotorSpeedDisplay display_;
    Config config_;
    float submitted_rpm_[4] = {};
    float submitted_base_rpm_ = 0, submitted_diff_rpm_ = 0;
    uint32_t last_control_ms_ = 0;
    bool initialized_ = false;
    bool running_ = false;
    CoursePlanner course_;
    FinishLineGate finish_;
    uint32_t distance_ms_[4] = {};
    float wheel_distance_m_[4] = {};
    bool UpdateCourse(uint32_t now);
};
