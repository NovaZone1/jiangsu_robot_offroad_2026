#include "LegacyLineRun.hpp"
#include "MotorSpeedProfiles.hpp"
#include "bsp_oled_bus.h"
#include "stm32f1xx_hal.h"
#include <assert.h>
#include <limits>
#include <stdio.h>
#include <string>

TestDwt test_dwt = {};
TestCoreDebug test_core_debug = {};
uint32_t test_tick = 0, test_primask = 0;
static bool calibrated = false, gray_valid = true, gray_frozen = false;
static bool range_valid = true, range_frozen = false, display_error = false;
static uint8_t white_mask = 0x18;
static float distance_mm = 40;
static uint32_t range_ms = 0;

struct Plant
{
    float rpm = 0, duty = 0;
    bool feedback = true, simulate = true, driver = true;
    uint32_t sample_ms = 0;
} plants[4];

static DcMotor m1, m2, m3, m4;
static DcMotor *motors[] = {&m1, &m2, &m3, &m4};
static GraySensor gray;
static Ultrasonic range;
static LineFollowApp line(gray, m1, m2, m3, m4);
static MotorProtection protection(m1, m2, m3, m4);
static LegacyLineRun app(gray, range, line, m1, m2, m3, m4, protection);

extern "C" uint8_t BspOledBus_Init()
{
    return 1;
}

extern "C" uint8_t BspOledBus_Send(uint8_t, const uint8_t *, uint16_t)
{
    return 1;
}

extern "C" BspOledBus_Status BspOledBus_Poll()
{
    return display_error ? BspOledBus_Error : BspOledBus_Idle;
}

static void CheckStopped()
{
    for (unsigned i = 0; i < 4; ++i)
    {
        assert(!motors[i]->IsEnabled() && plants[i].duty == 0);
    }
}

static void Tick(unsigned count)
{
    while (count--)
    {
        ++test_tick;
        for (unsigned i = 0; i < 4; ++i)
        {
            if (plants[i].simulate)
            {
                plants[i].rpm += (plants[i].duty * (250 + i * 5) - plants[i].rpm) * .001f / .18f;
            }
        }
        app.Run();
    }
}

static void GateTests()
{
    UltrasonicStartGate gate;
    const uint32_t hold = LegacyLineRun::StartHoldMs;
    const float near = LegacyLineRun::StartDistanceMm;
    const uint32_t trigger = ((hold + 59U) / 60U) * 60U;
    for (uint32_t t = 0; t < trigger; t += 60)
    {
        assert(!gate.Update({40, t, true}, t, true, hold, near));
    }
    assert(gate.Update({40, trigger, true}, trigger, true, hold, near));
    assert(!gate.Update({40, trigger + 60, true}, trigger + 60, true, 0));
    gate.Reset();
    for (uint32_t t = 0; t < trigger; t += 60)
    {
        assert(!gate.Update({40, t, true}, t, true, hold, near));
    }
    assert(!gate.Update({near, trigger, true}, trigger, true, hold,
                        near)); // Strictly below the configured distance.
    assert(gate.ElapsedMs() == 0);
    assert(!gate.Update({40, trigger + 60, true}, trigger + 60, true, hold, near));
    assert(!gate.Update({40, trigger + 120, false}, trigger + 120, true, hold, near));
    assert(gate.ElapsedMs() == 0);
    assert(!gate.Update({40, trigger + 180, true}, trigger + 180, true, hold, near));
    assert(!gate.Update({40, trigger + 300, true}, trigger + 300, true, hold,
                        near)); // Fresh gap exceeds 100 ms.
    assert(gate.ElapsedMs() == 0);
    gate.Reset();
    for (uint32_t t = 0; t < 6000; ++t)
    {
        assert(
            !gate.Update({40, 0, true}, t, true, hold, near)); // Cached result cannot start motion.
    }
    assert(!gate.Update({std::numeric_limits<float>::quiet_NaN(), 6000, true}, 6000, true, hold,
                        near));
    assert(!gate.Update({0, 6060, true}, 6060, true, hold, near));
    assert(!gate.Update({40, 6120, true}, 6120, false, hold, near));
    assert(!gate.Update({40, 6120, true}, 6150, true, hold, near)); // Cached before readiness.
    assert(gate.ElapsedMs() == 0);
    gate.Reset();
    for (uint32_t dt = 0; dt < trigger; dt += 60)
    {
        const uint32_t t = UINT32_MAX - 599U + dt;
        assert(!gate.Update({40, t, true}, t, true, hold, near));
    }
    assert(gate.Update({40, UINT32_MAX - 599U + trigger, true}, UINT32_MAX - 599U + trigger, true,
                       hold, near));
}

int main(int argc, char **argv)
{
    const std::string scenario = argc > 1 ? argv[1] : "normal";
    if (scenario == "gate")
    {
        GateTests();
        puts("PASS: selected fresh near duration/distance, resets, cached/frozen/invalid, "
             "wrap");
        return 0;
    }
    if (scenario == "near70")
    {
        distance_mm = 70; // Only the new <100 mm build can start here.
    }
    if (scenario == "wrap")
    {
        test_tick = UINT32_MAX - 22000U;
    }
    System.Init();
    System.BindStopHandler(
        []()
        {
            for (auto motor : motors)
            {
                motor->Disable();
            }
        });
    gray.Bind(nullptr,
              [](void *, GraySensor::Sample *sample)
              {
                  if (gray_frozen)
                  {
                      return false;
                  }
                  *sample = {};
                  sample->channels = 8;
                  sample->timestamp_ms = test_tick;
                  sample->valid = calibrated && gray_valid;
                  for (unsigned i = 0; i < 8; ++i)
                  {
                      sample->line[i] = white_mask & (1U << i) ? 1 : 0;
                  }
                  return true;
              });
    range_ms = test_tick;
    range.Bind(nullptr,
               [](void *, Ultrasonic::Sample *sample)
               {
                   if (range_frozen || (uint32_t)(test_tick - range_ms) < 60U)
                   {
                       return false;
                   }
                   range_ms = test_tick;
                   *sample = {distance_mm, test_tick, range_valid};
                   return true;
               });
    for (unsigned i = 0; i < 4; ++i)
    {
        DcMotor::Driver driver = {};
        driver.context = &plants[i];
        driver.set_duty = [](void *p, float duty)
        {
            assert(fabsf(duty) <= .85001f);
            auto &plant = *static_cast<Plant *>(p);
            plant.duty = duty;
            return plant.driver;
        };
        driver.stop = [](void *p)
        {
            static_cast<Plant *>(p)->duty = 0;
        };
        driver.read_rpm = [](void *p, float *rpm)
        {
            auto &plant = *static_cast<Plant *>(p);
            if (!plant.feedback || (uint32_t)(test_tick - plant.sample_ms) < 10U)
            {
                return false;
            }
            plant.sample_ms = test_tick;
            *rpm = plant.rpm;
            return true;
        };
        assert(motors[i]->Init(driver) && MotorSpeedProfiles::Apply(*motors[i], i + 1, false));
    }
    assert(protection.Init() && line.Init(LegacyLineRun::Configuration()));
    assert(System.RegistApp(line));
    assert(app.Init(
        [](bool value)
        {
            calibrated = value;
        }));
    if (scenario == "early_stop")
    {
        app.Stop();
        Tick(30000);
        CheckStopped();
        assert(!app.Init(
            [](bool)
            {
            }));
        return 0;
    }
    assert(calibrated);
    const auto cfg = line.GetTrackConfig();
    assert(cfg.base_rpm == 120 && cfg.kp == 6 && cfg.ki == 0 && cfg.kd == .2f);
    assert(cfg.turn_rpm == 72 && cfg.maximum_diff_rpm == 60 && cfg.gap_hold_ms == 207);
    assert(cfg.curve_boost_rpm == 0 && cfg.curve_speed_drop_rpm == 0);
    Tick(1100);
    CheckStopped();
    assert(calibrated);
    if (scenario == "one_second" || scenario == "near70")
    {
        assert(LegacyLineRun::StartHoldMs == 1000U && LegacyLineRun::StartDistanceMm == 100);
        Tick(850);
        CheckStopped();
        Tick(300);
        assert(app.GetStatus().phase == LegacyLineRun::Phase::Running && test_tick < 2500);
        app.Stop();
        Tick(7000);
        CheckStopped();
        return 0;
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        const auto &pid = motors[i]->GetSpeedPidConfig();
        assert(pid.kp == LegacyLineRun::MotorKp && pid.ki == LegacyLineRun::MotorKi &&
               pid.kd == 0 && pid.integral_limit == .85f && pid.derivative_on_measurement);
    }
    if (scenario == "boundary" || scenario == "range_invalid" || scenario == "cached")
    {
        if (scenario == "boundary")
        {
            distance_mm = LegacyLineRun::StartDistanceMm;
        }
        if (scenario == "range_invalid")
        {
            range_valid = false;
        }
        if (scenario == "cached")
        {
            range_frozen = true;
        }
        Tick(10000);
        CheckStopped();
        assert(app.GetStatus().phase == LegacyLineRun::Phase::Waiting);
        puts("PASS: near boundary/invalid/cached range never starts line motion");
        return 0;
    }
    if (scenario == "no_line" || scenario == "wide_line" || scenario == "separated_line")
    {
        white_mask = scenario == "wide_line" ? 0xFF : scenario == "separated_line" ? 0x81 : 0;
        Tick(10000);
        CheckStopped();
        assert(app.GetStatus().hold_ms == 0);
        return 0;
    }
    if (scenario == "external_enable")
    {
        assert(m2.Enable());
        Tick(1);
        CheckStopped();
        assert(app.GetStatus().phase == LegacyLineRun::Phase::Fault);
        return 0;
    }
    if (scenario == "reset_hold")
    {
        Tick(LegacyLineRun::StartHoldMs / 2U);
        distance_mm = LegacyLineRun::StartDistanceMm + 10;
        Tick(100);
        assert(app.GetStatus().hold_ms == 0);
        distance_mm = 40;
        Tick(LegacyLineRun::StartHoldMs - 100U);
        CheckStopped();
        Tick(2000);
    }
    else
    {
        Tick(9000);
    }
    assert(app.GetStatus().phase == LegacyLineRun::Phase::Running);
    for (unsigned i = 0; i < 4; ++i)
    {
        const float expected = LegacyLineRun::TargetRpm;
        assert(motors[i]->IsEnabled() && fabsf(motors[i]->GetTargetSpeed() - expected) < .01f);
    }
    if (scenario == "target_bound")
    {
        assert(m1.SetSpeed(120.01f));
        Tick(1);
        assert(app.GetStatus().failure == LegacyLineRun::Failure::TargetRange);
    }
    else if (scenario == "dashed")
    {
        for (unsigned gap = 0; gap < 7; ++gap)
        {
            white_mask = 0;
            Tick(150);
            assert(line.GetTrackCommand().state == Track::State::GapHold);
            assert(m1.GetTargetSpeed() > 119.9f && m3.GetTargetSpeed() > 119.9f);
            white_mask = 0x18;
            Tick(250);
            assert(line.GetTrackCommand().state == Track::State::Following);
        }
        app.Stop();
    }
    else if (scenario == "normal" || scenario == "wrap" || scenario == "reset_hold")
    {
        distance_mm = 1000; // Releasing the start target is not a second stop/start gesture.
        Tick(1000);
        white_mask = 1;
        Tick(500);
        assert(m1.GetTargetSpeed() == m2.GetTargetSpeed() &&
               m3.GetTargetSpeed() == m4.GetTargetSpeed());
        assert(m1.GetTargetSpeed() < m3.GetTargetSpeed() && m3.GetTargetSpeed() <= 180);
        white_mask = 0x80;
        Tick(700);
        assert(m1.GetTargetSpeed() > m3.GetTargetSpeed() && m1.GetTargetSpeed() <= 180);
        display_error = true;
        Tick(100);
        assert(app.GetStatus().phase == LegacyLineRun::Phase::Running);
        app.Stop();
    }
    else if (scenario == "gray_stale")
    {
        gray_frozen = true;
        Tick(60);
    }
    else if (scenario == "loss")
    {
        white_mask = 0;
        Tick(1300);
    }
    else if (scenario == "gap")
    {
        test_tick += 60;
        Tick(1);
    }
    else
    {
        const unsigned i = scenario.back() - '1';
        assert(i < 4);
        const auto fault = scenario.substr(0, scenario.size() - 2);
        if (fault == "disabled")
        {
            motors[i]->Disable();
        }
        else if (fault == "feedback")
        {
            plants[i].feedback = false;
        }
        else if (fault == "driver")
        {
            plants[i].driver = false;
        }
        else
        {
            plants[i].simulate = false;
            if (fault == "overspeed")
            {
                plants[i].rpm = 217;
            }
            else if (fault == "direction")
            {
                plants[i].rpm = -40;
            }
            else if (fault == "stall")
            {
                plants[i].rpm = 0;
            }
            else if (fault == "saturation")
            {
                plants[i].rpm = 40;
            }
            else
            {
                assert(false);
            }
        }
        Tick(1900);
    }
    CheckStopped();
    assert(app.GetStatus().phase == LegacyLineRun::Phase::Stopped ||
           app.GetStatus().phase == LegacyLineRun::Phase::Fault);
    assert(!app.Init(
        [](bool)
        {
        }));
    white_mask = 0x18;
    distance_mm = 40;
    Tick(7000);
    CheckStopped();
    printf("PASS: local line %s, fixed PI/85pct/selected targets, safety and stop latch\n",
           scenario.c_str());
}
