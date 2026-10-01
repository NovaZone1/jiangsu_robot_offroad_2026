#pragma once
#include <stdint.h>

/* New module extension point: measurements are supplied by a nonblocking
 * trigger/echo or UART driver once the sensor model has been selected. */
class Ultrasonic
{
public:
    struct Sample
    {
        float distance_mm;
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
    bool HasFreshSample(uint32_t now_ms, uint32_t max_age_ms = 200) const;

    const Sample &GetSample() const
    {
        return sample_;
    }

private:
    void *context_ = nullptr;
    Reader reader_ = nullptr;
    Sample sample_ = {};
};
