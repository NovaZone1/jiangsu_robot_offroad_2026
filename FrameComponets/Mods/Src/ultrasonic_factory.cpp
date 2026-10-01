#include "ultrasonic_factory.hpp"
#include "bsp_ultrasonic_echo.h"
#include "stm32f1xx_hal.h"

namespace
{
    constexpr uint32_t kPeriodMs = 60U;
    constexpr uint32_t kTimeoutMs = 35U;
    constexpr uint32_t kFreshMs = 200U;
    constexpr uint32_t kMaxPulseUs = 29250U; // 500 cm * 58.5 us/cm.
    Ultrasonic *bound_sensor = nullptr;
    FactoryUltrasonic::Status status = FactoryUltrasonic::Status::NotInitialized;
    uint32_t last_start_ms = 0;
    bool pending = false;

    bool ReadSample(void *, Ultrasonic::Sample *sample)
    {
        const uint32_t now = HAL_GetTick();
        if (!sample || !bound_sensor)
        {
            return false;
        }
        if (pending)
        {
            BspUltrasonicEcho_Sample echo = {};
            if (BspUltrasonicEcho_Poll(&echo))
            {
                pending = false;
                BspUltrasonicEcho_Cancel();
                sample->timestamp_ms = echo.timestamp_ms;
                sample->valid = echo.valid && echo.pulse_us > 0U &&
                                echo.pulse_us <= kMaxPulseUs &&
                                (uint32_t)(echo.timestamp_ms - last_start_ms) < kTimeoutMs;
                sample->distance_mm = sample->valid ? echo.pulse_us * (10.0f / 58.5f) : 0.0f;
                status = sample->valid ? FactoryUltrasonic::Status::Ok
                                       : FactoryUltrasonic::Status::InvalidEcho;
                return true; // Invalid NEW result explicitly invalidates the old sample.
            }
            if ((uint32_t)(now - last_start_ms) >= kTimeoutMs)
            {
                BspUltrasonicEcho_Cancel();
                pending = false;
                *sample = {0.0f, now, false};
                status = FactoryUltrasonic::Status::Timeout;
                return true;
            }
            return false;
        }
        if ((uint32_t)(now - last_start_ms) < kPeriodMs)
        {
            return false;
        }
        last_start_ms = now;
        if (!BspUltrasonicEcho_Start())
        {
            BspUltrasonicEcho_Cancel();
            *sample = {0.0f, now, false};
            status = FactoryUltrasonic::Status::HardwareError;
            return true;
        }
        pending = true;
        return false; // Keep a previous fresh result while the next ping is in flight.
    }
} // namespace

bool FactoryUltrasonic::Init(Ultrasonic &sensor)
{
    if (bound_sensor)
    {
        return bound_sensor == &sensor; // Never rebind a different device silently.
    }
    if (sensor.IsReady() || !BspUltrasonicEcho_Init())
    {
        status = Status::HardwareError;
        return false;
    }
    bound_sensor = &sensor;
    pending = false;
    last_start_ms = HAL_GetTick(); // Allow 60 ms for initial settling.
    status = Status::Waiting;
    sensor.Bind(nullptr, ReadSample);
    return true;
}

FactoryUltrasonic::Status FactoryUltrasonic::ReadDistanceCm(float &cm)
{
    cm = -1.0f;
    if (!bound_sensor || status != Status::Ok)
    {
        return status;
    }
    if (!bound_sensor->HasFreshSample(HAL_GetTick(), kFreshMs))
    {
        return Status::Stale;
    }
    cm = bound_sensor->GetSample().distance_mm * 0.1f;
    return Status::Ok;
}
