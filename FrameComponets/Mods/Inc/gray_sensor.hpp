#pragma once
#include "frame_config.h"
#include <stdint.h>

class GraySensor
{
public:
    struct Sample
    {
        float line[FRAME_GRAY_MAX_CHANNELS]; // calibrated [0,1], 1 = WHITE guide line
        uint8_t channels;
        uint32_t timestamp_ms;
        bool valid;
    };

    using Reader = bool (*)(void *, Sample *);

    void Bind(void *context, Reader reader)
    {
        context_ = context;
        reader_ = reader;
        sample_ = {};
    }

    bool IsReady() const
    {
        return reader_ != nullptr;
    }

    bool Update();
    bool HasFreshSample(uint32_t now_ms, uint32_t max_age_ms = 50) const;

    const Sample &GetSample() const
    {
        return sample_;
    }

    /* Weighted line centroid in [-1,1]; false means line not detected.
     * Distinguishing a dashed gap, black finish line and line loss is an App job. */
    bool GetLineError(float &error) const;

private:
    void *context_ = nullptr;
    Reader reader_ = nullptr;
    Sample sample_ = {};
};
