#include "ultrasonic.hpp"
#include <math.h>

bool Ultrasonic::Update()
{
    Sample next = {};
    if (!reader_ || !reader_(context_, &next))
    {
        return false; // no new sample
    }
    if (!next.valid || !isfinite(next.distance_mm) || next.distance_mm <= 0)
    {
        sample_.valid = false;
        return false;
    }
    sample_ = next;
    return true;
}

bool Ultrasonic::HasFreshSample(uint32_t now_ms, uint32_t max_age_ms) const
{
    return sample_.valid && (uint32_t)(now_ms - sample_.timestamp_ms) <= max_age_ms;
}

/* TODO: implement the selected sensor protocol here, including echo timeout,
 * rising/falling edge capture, timer rollover and minimum trigger interval.
 * Timestamp each NEW measurement; never refresh the timestamp of stale data.
 * Echo ISR should only save pulse data; convert distance in the task.
 * Candidate BSPs: bsp_dwt, bsp_gpio (EXTI), bsp_uart.
 * Gesture threshold/debounce belongs in Apps, not this measurement module. */
