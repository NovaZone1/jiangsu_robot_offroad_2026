#pragma once
#include "LineFollowApp.hpp"
#include "MotorProtection.hpp"
#include "UltrasonicStartGate.hpp"

#ifndef LINE_FOLLOW_ULTRASONIC_START
#define LINE_FOLLOW_ULTRASONIC_START 0
#endif

class LegacyLineRun
{
public:
    static constexpr bool Enabled = LINE_FOLLOW_ULTRASONIC_START != 0;
    static constexpr uint32_t StartHoldMs = 1000U;
    static constexpr float StartDistanceMm = 100;
    static constexpr float TargetRpm = 120;
    static constexpr float DefaultKp = 66.0f;
    static constexpr float DefaultKd = 2.2f;
    static constexpr float MotorKp = 0.001f;
    static constexpr float MotorKi = 0.010f;
    static constexpr float DutyLimit = 0.85f;
    enum class Phase
    {
        Disarmed,
        Warmup,
        Preparing,
        Waiting,
        Running,
        Stopped,
        Fault
    };
    enum class Failure
    {
        None,
        Initialization,
        Protection,
        Feedback,
        FeedbackRange,
        UnexpectedEnable,
        UnexpectedDisable,
        TargetRange,
        Saturation,
        Start,
        LostLine,
        SystemState
    };

    struct Status
    {
        Phase phase = Phase::Disarmed;
        Failure failure = Failure::None;
        uint8_t port = 0;
        uint32_t hold_ms = 0;
        uint8_t gray_mask = 0;
        Track::State track_state = Track::State::Disabled;
        float line_error = 0;
    };

    LegacyLineRun(GraySensor &gray, Ultrasonic &range, LineFollowApp &line, DcMotor &m1,
                  DcMotor &m2, DcMotor &m3, DcMotor &m4, MotorProtection &protection);
    static LineFollowApp::Config Configuration();
    bool Init(void (*confirm_calibration)(bool));
    void Run();
    void Stop();
    const Status &GetStatus() const;

private:
    void DisableAll();
    void Fail(Failure failure, uint8_t port = 0);
    bool CheckFeedback();
    bool HasStartLine() const;
    void UpdateDisplay();
    GraySensor &gray_;
    Ultrasonic &range_;
    LineFollowApp &line_;
    DcMotor *motors_[4];
    MotorProtection &protection_;
    FactoryOled display_;
    UltrasonicStartGate gate_;
    Status status_;
    uint32_t boot_ms_ = 0, system_ms_ = 0, started_ms_ = 0, display_ms_ = 0;
    uint32_t saturated_ms_[4] = {};
    bool saturated_[4] = {};
    bool attempted_ = false, ever_enabled_ = false;
};
