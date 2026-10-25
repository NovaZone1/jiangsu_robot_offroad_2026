#pragma once
#include "ultrasonic.hpp"
#include <math.h>

// Fresh consecutive measurements, not repeated reads of one cached near result.
class UltrasonicStartGate
{
public:
    void Reset()
    {
        holding_ = false;
        eligible_ = false;
        first_ms_ = last_ms_ = 0;
    }

    bool Update(const Ultrasonic::Sample &sample, uint32_t now, bool ready,
                uint32_t hold_ms = 5000U, float near_mm = 50)
    {
        if (!ready || hold_ms == 0 || hold_ms >= 0x80000000U || !isfinite(near_mm) ||
            near_mm <= 0 || !sample.valid || !isfinite(sample.distance_mm) ||
            sample.distance_mm <= 0 || sample.distance_mm >= near_mm ||
            (uint32_t)(now - sample.timestamp_ms) > 100U)
        {
            Reset();
            return false;
        }
        if (!eligible_)
        {
            eligible_ = true;
            eligible_ms_ = now;
        }
        if ((uint32_t)(sample.timestamp_ms - eligible_ms_) >= 0x80000000U)
        {
            return false; // Do not count a cached measurement from before readiness.
        }
        if (holding_ && sample.timestamp_ms == last_ms_)
        {
            return false;
        }
        if (!holding_ || (uint32_t)(sample.timestamp_ms - last_ms_) > 100U)
        {
            first_ms_ = sample.timestamp_ms;
        }
        holding_ = true;
        last_ms_ = sample.timestamp_ms;
        return (uint32_t)(last_ms_ - first_ms_) >= hold_ms;
    }

    uint32_t ElapsedMs() const
    {
        return holding_ ? (uint32_t)(last_ms_ - first_ms_) : 0;
    }

private:
    uint32_t first_ms_ = 0, last_ms_ = 0, eligible_ms_ = 0;
    bool holding_ = false, eligible_ = false;
};
