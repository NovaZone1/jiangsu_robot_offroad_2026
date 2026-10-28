#pragma once
#include <math.h>
#include <stdint.h>

// Course progress and cruise reference only; steering belongs to Track's two PIDs.
class CoursePlanner
{
public:
    enum class Phase
    {
        Low,
        Accelerating,
        Fast,
        Decelerating,
        Approach,
        Curve,
        Exit,
        Invalid
    };

    struct Status
    {
        Phase phase = Phase::Low;
        float distance_m = 0, leg_distance_m = 0, cruise_rpm = 120;
        uint8_t completed_bends = 0;
        bool restricted = false, invalid = false;
    };

    void Reset(uint32_t now)
    {
        *this = CoursePlanner();
        last_ms_ = now;
    }

    const Status &GetStatus() const
    {
        return status_;
    }

    const Status &Update(uint32_t now, float distance, bool centered, bool reliable, bool turning,
                         bool tracking)
    {
        if (status_.invalid)
        {
            return status_;
        }
        if ((uint32_t)(now - last_ms_) >= 50 || !isfinite(distance) || distance < 0 ||
            distance + .001f < status_.distance_m)
        {
            status_.invalid = true;
            status_.phase = Phase::Invalid;
            return status_;
        }
        last_ms_ = now;
        status_.distance_m = distance;
        status_.leg_distance_m = distance - leg_origin_;
        if (centered && reliable && tracking)
        {
            if (!centering_)
            {
                center_ms_ = now;
            }
            centering_ = true;
        }
        else
        {
            centering_ = false;
        }
        const bool stable = centering_ && (uint32_t)(now - center_ms_) >= 300;
        if (reliable)
        {
            seen_line_ = true;
            missing_ = false;
        }
        else if (seen_line_)
        {
            if (!missing_)
            {
                missing_ms_ = now;
            }
            missing_ = true;
            if ((uint32_t)(now - missing_ms_) >= 50)
            {
                status_.restricted = true;
            }
        }
        if (turning)
        {
            if (!curve_candidate_)
            {
                curve_ms_ = now;
                curve_distance_ = distance;
                curve_candidate_ = true;
            }
            if (status_.leg_distance_m >= 5.5f && (uint32_t)(now - curve_ms_) >= 800 &&
                distance - curve_distance_ >= .3f)
            {
                completed_curve_ = true;
            }
            status_.phase = Phase::Curve;
            status_.cruise_rpm = 120;
            return status_;
        }
        curve_candidate_ = false;
        if (completed_curve_)
        {
            status_.cruise_rpm = 120;
            if (status_.phase == Phase::Curve)
            {
                status_.phase = Phase::Exit;
                phase_ms_ = now;
            }
            if (stable && (uint32_t)(now - phase_ms_) >= 500)
            {
                if (status_.completed_bends < 255)
                {
                    ++status_.completed_bends;
                }
                leg_origin_ = distance;
                status_.leg_distance_m = 0;
                status_.restricted = false;
                completed_curve_ = false;
                status_.phase = Phase::Low;
            }
            return status_;
        }
        if (status_.phase == Phase::Curve)
        {
            status_.phase = Phase::Low;
        }
        const float position = status_.leg_distance_m;
        const float length = status_.completed_bends ? 8.f : 7.5f;
        const float slow_end = status_.completed_bends % 2 ? 5.6f : 4.8f;
        const bool slow = position >= 2.4f && position <= slow_end;
        const float boundary = position < 2.4f ? 2.4f : length;
        const float up_distance = .20106193f * 150 / 60 * 2;
        const float down_distance = .20106193f * 150 / 60 * 1.2f;
        const bool final_approach = status_.completed_bends >= 6;
        const bool reduce = slow || position >= boundary - down_distance - .5f || !reliable ||
                            !centered || !tracking || status_.restricted || final_approach;
        if ((status_.phase == Phase::Accelerating || status_.phase == Phase::Fast) && reduce)
        {
            ramp_from_ = status_.cruise_rpm;
            phase_ms_ = now;
            status_.phase = Phase::Decelerating;
        }
        if (status_.phase == Phase::Accelerating)
        {
            const float u = fminf(1, (float)(now - phase_ms_) / 2000);
            status_.cruise_rpm = 120 + 30 * (1 - cosf(3.14159265f * u));
            if (u >= 1)
            {
                status_.phase = Phase::Fast;
            }
        }
        else if (status_.phase == Phase::Fast)
        {
            status_.cruise_rpm = 180;
        }
        else if (status_.phase == Phase::Decelerating)
        {
            const float u = fminf(1, (float)(now - phase_ms_) / 1200);
            status_.cruise_rpm = 120 + (ramp_from_ - 120) * .5f * (1 + cosf(3.14159265f * u));
            if (u >= 1)
            {
                status_.phase = Phase::Low;
            }
        }
        else
        {
            status_.cruise_rpm = 120;
            if (position >= length - .5f || final_approach)
            {
                status_.phase = Phase::Approach;
            }
            if (status_.phase != Phase::Approach && stable && !status_.restricted && !slow &&
                position >= .6f && boundary - position > up_distance + down_distance + .8f)
            {
                phase_ms_ = now;
                status_.phase = Phase::Accelerating;
            }
        }
        return status_;
    }

    void NoteLowerMarker(uint8_t crossings, float distance)
    {
        if (crossings && crossings % 2 == 0)
        {
            leg_origin_ = distance - .5f;
            status_.restricted = false;
        }
    }

private:
    Status status_;
    uint32_t last_ms_ = 0, center_ms_ = 0, missing_ms_ = 0, curve_ms_ = 0, phase_ms_ = 0;
    float leg_origin_ = 0, curve_distance_ = 0, ramp_from_ = 120;
    bool centering_ = false, seen_line_ = false, missing_ = false, curve_candidate_ = false,
         completed_curve_ = false;
};
