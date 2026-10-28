#pragma once
#include <math.h>
#include <stdint.h>

// A contextual black-tape candidate, NOT a new ability to distinguish reflectance.
// Green and black both yield mask 0. The course windows and pulse width must be
// checked on the real selected lane; white must actually return after each pulse.
class FinishLineGate
{
public:
    enum class Phase
    {
        Watching,
        Blank,
        Confirming,
        Clearing,
        Done
    };

    struct Status
    {
        Phase phase = Phase::Watching;
        uint8_t crossings = 0;
        float last_width_m = 0;
        bool invalid = false;
    };

    void Reset()
    {
        *this = FinishLineGate();
    }

    const Status &GetStatus() const
    {
        return status_;
    }

    const Status &Update(uint32_t sample_ms, uint32_t now, uint8_t mask, bool reliable,
                         bool straight, float distance, float front_left, float front_right,
                         uint8_t bends, float leg_distance)
    {
        if (status_.phase == Phase::Done || status_.invalid)
        {
            return status_;
        }
        if (!isfinite(distance) || !isfinite(front_left) || !isfinite(front_right) ||
            (uint32_t)(now - sample_ms) >= 50)
        {
            status_.invalid = true;
            return status_;
        }
        if (sampled_ && sample_ms == last_sample_)
        {
            return status_;
        }
        if (sampled_ && (uint32_t)(sample_ms - last_sample_) >= 50)
        {
            status_.invalid = true;
            return status_;
        }
        sampled_ = true;
        last_sample_ = sample_ms;
        const bool white = CenterWhite(mask);
        if (status_.phase == Phase::Clearing)
        {
            // No blind drive after the tape; original gray/loss guards also run.
            if (!white || !reliable)
            {
                status_.invalid = true;
                return status_;
            }
            if (front_left - end_left_ >= .04f && front_right - end_right_ >= .04f)
            {
                status_.phase = Phase::Done;
            }
            return status_;
        }
        if (status_.phase == Phase::Blank)
        {
            if ((uint32_t)(sample_ms - blank_ms_) > 500 || distance - blank_distance_ > .095f)
            {
                Cancel();
                return status_;
            }
            if (mask == 0)
            {
                return status_;
            }
            const float width = distance - blank_distance_;
            // Rule: black 60mm, dashed gaps 50mm. A location window remains
            // mandatory; width alone is unreliable under slip/height variations.
            if (!white || width < .055f || width > .085f)
            {
                Cancel();
                return status_;
            }
            status_.last_width_m = width;
            end_left_ = front_left;
            end_right_ = front_right;
            confirm_ms_ = sample_ms;
            confirmations_ = 1;
            status_.phase = Phase::Confirming;
            return status_;
        }
        if (status_.phase == Phase::Confirming)
        {
            if (!white || (uint32_t)(sample_ms - confirm_ms_) > 40)
            {
                Cancel();
                return status_;
            }
            ++confirmations_;
            if (confirmations_ >= 3 && reliable)
            {
                ++status_.crossings;
                last_crossing_m_ = distance;
                if (status_.crossings == 6)
                {
                    status_.phase = Phase::Clearing;
                }
                else
                {
                    Cancel();
                }
            }
            return status_;
        }
        // Depart just beyond mark 1. On each lap: mark 2, right bend,
        // upper dashed straight, left bend, then own mark 1. Six validated
        // alternating crossings are THREE laps, never "two black pulses".
        const bool expect_mark2 = status_.crossings % 2 == 0;
        const bool lower_segment =
            expect_mark2
                ? bends == status_.crossings &&
                      (status_.crossings == 0 ? distance >= 5.5f && distance <= 7.3f
                                              : leg_distance >= 6.4f && leg_distance <= 8.0f)
                : bends == status_.crossings + 1 && leg_distance >= .05f && leg_distance <= 1.5f;
        if (mask == 0 && white_ready_ && (uint32_t)(sample_ms - last_white_ms_) <= 10 &&
            lower_segment && straight && distance - last_crossing_m_ >= .5f)
        {
            status_.phase = Phase::Blank;
            blank_ms_ = sample_ms;
            blank_distance_ = distance;
            white_ready_ = false;
        }
        else if (white && reliable && straight)
        {
            if (!white_pending_)
            {
                white_ms_ = sample_ms;
            }
            white_pending_ = true;
            last_white_ms_ = sample_ms;
            white_ready_ = (uint32_t)(sample_ms - white_ms_) >= 80;
        }
        else
        {
            white_ready_ = white_pending_ = false;
        }
        return status_;
    }

private:
    static bool CenterWhite(uint8_t mask)
    {
        if (!mask)
        {
            return false;
        }
        unsigned first = 0, last = 7;
        while (!(mask & (1U << first)))
        {
            ++first;
        }
        while (!(mask & (1U << last)))
        {
            --last;
        }
        const unsigned expected = ((1U << (last - first + 1)) - 1) << first;
        return mask == expected && last - first < 4 && fabsf(((float)first + last - 7) / 7) <= .35f;
    }

    void Cancel()
    {
        status_.phase = Phase::Watching;
        white_pending_ = white_ready_ = false;
    }

    Status status_;
    bool sampled_ = false, white_pending_ = false, white_ready_ = false;
    uint32_t last_sample_ = 0, white_ms_ = 0, last_white_ms_ = 0;
    uint32_t blank_ms_ = 0, confirm_ms_ = 0;
    uint8_t confirmations_ = 0;
    float blank_distance_ = 0, last_crossing_m_ = -1;
    float end_left_ = 0, end_right_ = 0;
};
