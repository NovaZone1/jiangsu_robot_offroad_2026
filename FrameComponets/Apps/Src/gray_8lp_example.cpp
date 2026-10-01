#include "gray_8lp_example.hpp"
#include <math.h>

Gray8LpExample::Result Gray8LpExample::EvaluateBlackLine(const GraySensor::Sample &sample,
                                                         uint32_t now_ms, uint32_t max_age_ms)
{
    Result result = {Invalid, 0, 0.0f};
    if (!sample.valid || sample.channels != 8 ||
        (uint32_t)(now_ms - sample.timestamp_ms) > max_age_ms)
    {
        return result;
    }
    int moment = 0;
    uint8_t count = 0, runs = 0;
    bool previous_black = false;
    for (uint8_t i = 0; i < 8; ++i)
    {
        if (!isfinite(sample.line[i]) || sample.line[i] < 0.0f || sample.line[i] > 1.0f)
        {
            return {Invalid, 0, 0.0f};
        }
        // For this digital driver line[] is exactly 0/1; 0.5 is not an ADC threshold.
        const bool black = sample.line[i] < 0.5f;
        if (black)
        {
            result.black_mask |= (uint8_t)(1U << i);
            moment += 2 * (int)i - 7;
            ++count;
            if (!previous_black)
            {
                ++runs;
            }
        }
        previous_black = black;
    }
    if (count == 0)
    {
        result.position = NoLine;
    }
    else if (count == 8 || runs != 1)
    {
        result.position = Ambiguous; // Crossings/multiple lines need route context.
    }
    else
    {
        result.error = (float)moment / (7.0f * count);
        // Accept either center probe or the pair as centered in this simple example.
        result.position = moment < -(int)count ? Left : moment > count ? Right : Center;
    }
    return result;
}

uint8_t Gray8LpExample::ToFourZones(uint8_t black_mask)
{
    uint8_t zones = 0;
    for (uint8_t i = 0; i < 4; ++i)
    {
        if (black_mask & (3U << (2U * i)))
        {
            zones |= (uint8_t)(1U << i);
        }
    }
    return zones;
}
