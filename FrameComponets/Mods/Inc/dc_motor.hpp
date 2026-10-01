#pragma once
#include <stdint.h>

/* New module extension point. No H-bridge pins or polarity are assumed.
 * Implement the driver in Mods/Src/dc_motor.cpp after the motor driver and
 * encoder are chosen; bind it in Apps/MainFrame after CubeMX initialization. */
class DcMotor
{
public:
    struct Driver
    {
        void *context;
        bool (*set_duty)(void *, float signed_duty); // [-1,1]
        void (*stop)(void *); // board-defined coast/brake, must always stop
        bool (*read_rpm)(void *, float *rpm); // optional encoder feedback
    };

    bool Bind(const Driver &driver);

    bool IsReady() const
    {
        return driver_.set_duty && driver_.stop;
    }

    bool SetDuty(float duty);
    void Stop();
    bool ReadRpm(float &rpm);

    float GetDuty() const
    {
        return duty_;
    }

private:
    Driver driver_ = {};
    float duty_ = 0;
};
