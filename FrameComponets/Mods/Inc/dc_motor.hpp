#pragma once

#include "frame_config.h"
#include "pid.hpp"
#include <stdint.h>

// 仅参考 MotorDJI 的控制接口；输出是 PWM 占空比，不是 CAN 电流。
class DcMotor
{
public:
    enum class Mode
    {
        Duty,
        Speed
    };
    enum class Fault
    {
        None,
        InvalidCommand,
        OutputFailure,
        FeedbackLost,
        CommandTimeout
    };

    struct Driver
    {
        void *context;
        bool (*set_duty)(void *, float signed_duty);
        void (*stop)(void *);
        // true 表示新的、有效的输出轴 RPM；无新数据返回 false。
        bool (*read_rpm)(void *, float *rpm);
    };

    struct Measure
    {
        float speed_rpm = 0;
        uint32_t timestamp_ms = 0;
        uint32_t sample_count = 0;
        bool valid = false;
    };

    struct SpeedPidConfig
    {
        float kp = 0;
        float ki = 0;
        float kd = 0;
        float integral_limit = 0.25f; // 占空比单位
        float derivative_filter = 0.9f;
    };

    DcMotor() = default;
    ~DcMotor();
    DcMotor(const DcMotor &) = delete;
    DcMotor &operator=(const DcMotor &) = delete;

    // 初始化后必须显式 Enable；Bind 保留原应用入口，但同样默认禁能。
    bool Init(const Driver &driver, Mode mode = Mode::Duty);
    bool Bind(const Driver &driver);
    bool SwitchMode(Mode mode);
    bool Enable();
    void Disable();
    bool IsEnabled() const;
    bool IsReady() const;

    bool SetDuty(float signed_duty); // [-1,1]，开环模式，Enable 后生效
    bool SetSpeed(float output_rpm); // 输出轴 RPM，Speed 模式
    void Neutral(); // 清除目标及 PID 状态，保持使能状态
    void Stop(); // 与 Neutral 相同，兼容应用的停车接口
    float Control(); // 每个基础周期调用，返回最近接受的占空比指令
    static void ControlAllMotors();

    bool DutyLimSet(float limit);
    bool SpeedLimSet(float output_rpm);
    bool ConfigureSpeedPid(const SpeedPidConfig &config);
    bool SetTimeouts(uint32_t command_ms, uint32_t feedback_ms);
    bool ClearFault(); // 仅禁能后允许清除故障

    bool ReadRpm(float &rpm);
    bool IsOnline() const; // 仅表示 RPM 样本新鲜，不证明电机/接线正常
    const Measure &GetMeasure() const;
    Mode GetMode() const;
    Fault GetFault() const;
    float GetDuty() const;
    float GetTargetSpeed() const;

private:
    bool Register();
    bool UpdateFeedback(uint32_t now);
    bool WriteOutput(float duty);
    void Trip(Fault fault);

    static DcMotor *motors_[FRAME_MAX_MOTORS];
    Driver driver_ = {};
    Measure measure_;
    PidGeneral speed_pid_;
    Mode mode_ = Mode::Duty;
    Fault fault_ = Fault::None;
    bool enabled_ = false;
    bool pid_configured_ = false;
    bool has_command_ = false;
    float duty_ = 0;
    float target_duty_ = 0;
    float target_speed_ = 0;
    float duty_limit_ = 1;
    float speed_limit_ = 0; // 未确认额定转速前，不猜测闭环速度上限
    uint32_t command_tick_ = 0;
    uint32_t pid_tick_ = 0;
    uint32_t command_timeout_ms_ = 250;
    uint32_t feedback_timeout_ms_ = 50;
};

using MotorPWM = DcMotor;
