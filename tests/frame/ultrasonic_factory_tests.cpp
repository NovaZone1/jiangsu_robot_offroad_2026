#include "ultrasonic_factory.hpp"
#include "bsp_ultrasonic_echo.h"
#include "stm32f1xx_hal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

TestDwt test_dwt = {};
TestCoreDebug test_core_debug = {};
uint32_t test_tick = 0, test_primask = 0;

static bool init_ok = false, start_ok = true, ready = false;
static uint32_t starts = 0, cancels = 0;
static BspUltrasonicEcho_Sample result = {};

uint8_t BspUltrasonicEcho_Init(void)
{
    return init_ok;
}

uint8_t BspUltrasonicEcho_Start(void)
{
    ++starts;
    ready = false;
    return start_ok;
}

uint8_t BspUltrasonicEcho_Poll(BspUltrasonicEcho_Sample *sample)
{
    if (!ready)
    {
        return 0;
    }
    *sample = result;
    ready = false;
    return 1;
}

void BspUltrasonicEcho_Cancel(void)
{
    ++cancels;
    ready = false;
}

static void Deliver(uint32_t pulse_us, uint32_t timestamp_ms, bool valid = true)
{
    result = {pulse_us, timestamp_ms, (uint8_t)valid};
    ready = true;
    test_tick = timestamp_ms;
}

int main()
{
    using FactoryUltrasonic::Status;
    float cm = 123.0f;
    Ultrasonic sensor, other;
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::NotInitialized && cm == -1.0f);
    assert(!FactoryUltrasonic::Init(sensor) && !sensor.IsReady());
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::HardwareError);
    init_ok = true;
    test_tick = 0xFFFFFFE0U;
    assert(FactoryUltrasonic::Init(sensor));
    assert(FactoryUltrasonic::Init(sensor));
    assert(!FactoryUltrasonic::Init(other) && !other.IsReady());
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::Waiting && cm == -1.0f);

    // Startup interval and tick rollover, including repeated calls in one millisecond.
    test_tick = 27;
    assert(!sensor.Update() && starts == 0);
    test_tick = 28;
    assert(!sensor.Update() && starts == 1);
    for (int i = 0; i < 1000; ++i)
    {
        assert(!sensor.Update());
    }
    assert(starts == 1 && test_tick == 28);
    Deliver(5850, 34); // Factory conversion: 100 cm, internal 1000 mm.
    assert(sensor.Update());
    assert(fabsf(sensor.GetSample().distance_mm - 1000.0f) < 0.01f);
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::Ok && fabsf(cm - 100) < 0.01f);
    assert(!sensor.Update() && sensor.GetSample().timestamp_ms == 34);

    // No echo expires at 35 ms; old samples are explicitly invalidated.
    test_tick = 88;
    assert(!sensor.Update() && starts == 2);
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::Ok);
    test_tick = 122;
    assert(!sensor.Update());
    test_tick = 123;
    assert(!sensor.Update());
    assert(!sensor.HasFreshSample(test_tick));
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::Timeout && cm == -1.0f);

    // Out-of-range / zero / late completed pulses cannot revive an old measurement.
    test_tick = 148;
    assert(!sensor.Update());
    Deliver(29251, 178);
    assert(!sensor.Update());
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::InvalidEcho);
    test_tick = 208;
    assert(!sensor.Update());
    Deliver(0, 209);
    assert(!sensor.Update());
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::InvalidEcho);
    test_tick = 268;
    assert(!sensor.Update());
    Deliver(5850, 303);
    assert(!sensor.Update());
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::InvalidEcho);

    // Stuck-high / peripheral failure is reported and retried only after the interval.
    test_tick = 328;
    start_ok = false;
    assert(!sensor.Update());
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::HardwareError && cm == -1.0f);
    uint32_t attempts = starts;
    test_tick = 387;
    assert(!sensor.Update() && starts == attempts);
    start_ok = true;
    test_tick = 388;
    assert(!sensor.Update());
    Deliver(29250, 418);
    assert(sensor.Update());
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::Ok && fabsf(cm - 500) < 0.01f);

    // Reading alone does not refresh the timestamp, start a ping, or block.
    attempts = starts;
    test_tick = 618;
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::Ok);
    test_tick = 619;
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::Stale && cm == -1.0f);
    assert(starts == attempts && sensor.GetSample().timestamp_ms == 418);

    // Recovery, invalid hardware capture, and timeout across uint32_t wrap.
    assert(!sensor.Update());
    Deliver(585, 620, false);
    assert(!sensor.Update());
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::InvalidEcho);
    test_tick = 0xFFFFFFF0U;
    assert(!sensor.Update());
    test_tick = 19;
    assert(!sensor.Update());
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::Timeout);
    test_tick = 44;
    assert(!sensor.Update());
    Deliver(585, 45);
    assert(sensor.Update());
    assert(FactoryUltrasonic::ReadDistanceCm(cm) == Status::Ok && fabsf(cm - 10) < 0.01f);
    assert(cancels > 0);
    puts("PASS: factory ultrasonic cm/mm, interval, timeout, invalid echo, stale data, "
         "recovery, wrap");
}
