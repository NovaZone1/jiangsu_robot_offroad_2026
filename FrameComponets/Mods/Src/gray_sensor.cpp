#include "gray_sensor.hpp"
#include <math.h>

bool GraySensor::Update()
{
    Sample next = {};
    if (!reader_ || !reader_(context_, &next))
    {
        return false;
    }
    if (!next.valid || !next.channels || next.channels > FRAME_GRAY_MAX_CHANNELS)
    {
        sample_.valid = false;
        return false;
    }
    for (uint8_t i = 0; i < next.channels; ++i)
    {
        if (!isfinite(next.line[i]) || next.line[i] < 0 || next.line[i] > 1)
        {
            sample_.valid = false;
            return false;
        }
    }
    sample_ = next;
    return true;
}

bool GraySensor::HasFreshSample(uint32_t now_ms, uint32_t max_age_ms) const
{
    return sample_.valid && (uint32_t)(now_ms - sample_.timestamp_ms) <= max_age_ms;
}

bool GraySensor::GetLineError(float &error) const
{
    if (!sample_.valid || sample_.channels < 2)
    {
        return false;
    }
    float weight = 0, moment = 0;
    for (uint8_t i = 0; i < sample_.channels; ++i)
    {
        float position = 2.0f * i / (sample_.channels - 1U) - 1.0f;
        weight += sample_.line[i];
        moment += position * sample_.line[i];
    }
    if (weight < 0.01f)
    {
        return false;
    }
    error = moment / weight;
    return true;
}

/* TODO: implement GPIO/ADC/UART acquisition and calibration here for the
 * actual gray array. Normalize WHITE to 1 and background/black to 0.
 * Timestamp only NEW samples. Candidate BSPs: bsp_adc, bsp_gpio, bsp_uart. */
