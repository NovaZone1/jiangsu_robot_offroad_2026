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

static void TestFinishGate()
{
    FinishLineGate f;
    uint32_t t = 100;
    float d = 0;
    auto step = [&](uint8_t mask, uint8_t bends, float leg, bool reliable = true)
    {
        t += 5;
        f.Update(t, t, mask, reliable, true, d, d, d, bends, leg);
        assert(!f.GetStatus().invalid);
    };
    auto white = [&](uint8_t bends, float leg)
    {
        for (unsigned i = 0; i < 20; ++i)
        {
            step(0x18, bends, leg);
        }
    };
    auto pulse = [&](float width, uint8_t bends, float leg)
    {
        step(0, bends, leg, false);
        for (unsigned i = 0; i < 15; ++i)
        {
            d += width / 15;
            step(0, bends, leg, false);
        }
        for (unsigned i = 0; i < 4; ++i)
        {
            step(0x18, bends, leg);
        }
    };
    d = .05f;
    white(0, .05f);
    pulse(.06f, 0, .05f);
    assert(f.GetStatus().crossings == 0); // starting tape ignored
    d = 6.7f;
    white(0, 6.7f);
    pulse(.05f, 0, 6.7f);
    assert(f.GetStatus().crossings == 0); // 50mm gap, even in a marker window
    // First mark 2, then reject all seven upper dashed gaps, including a
    // deliberately distorted 60mm one: context is compulsory.
    d = 6.8f;
    white(0, 6.8f);
    pulse(.06f, 0, 6.8f);
    assert(f.GetStatus().crossings == 1);
    for (unsigned i = 0; i < 7; ++i)
    {
        d = 14 + i * .15f;
        white(1, 4);
        pulse(i == 3 ? .06f : .05f, 1, 4);
        assert(f.GetStatus().crossings == 1);
    }
    for (unsigned lap = 1; lap <= 3; ++lap)
    {
        d = 23.f * lap;
        white(2 * lap, .5f);
        pulse(.06f, 2 * lap, .5f);
        assert(f.GetStatus().crossings == 2 * lap);
        if (lap < 3)
        {
            assert(f.GetStatus().phase != FinishLineGate::Phase::Clearing);
            d += 6.9f;
            white(2 * lap, 7.4f);
            pulse(.06f, 2 * lap, 7.4f);
            assert(f.GetStatus().crossings == 2 * lap + 1);
        }
    }
    assert(f.GetStatus().phase == FinishLineGate::Phase::Clearing);
    d += .039f;
    step(0x18, 6, .6f);
    assert(f.GetStatus().phase == FinishLineGate::Phase::Clearing);
    d += .002f;
    step(0x18, 6, .6f);
    assert(f.GetStatus().phase == FinishLineGate::Phase::Done);
    f.Reset();
    d = 6.8f;
    white(0, 6.8f);
    step(0, 0, 6.8f, false);
    d += .12f;
    step(0, 0, 6.8f, false);
    white(0, 6.9f);
    assert(f.GetStatus().crossings == 0); // sustained loss is not a marker
    t += 50;
    f.Update(t, t, 0x18, true, true, d, d, d, 0, 6.9f);
    assert(f.GetStatus().invalid);
    f.Reset();
    t = UINT32_MAX - 60;
    d = 6.8f;
    white(0, 6.8f);
    pulse(.06f, 0, 6.8f);
    assert(f.GetStatus().crossings == 1); // wrap doesn't generate a lap
    puts("PASS finish gate: start exclusion, 50mm gap, distorted upper gaps, six alternating "
         "markers, three laps, real clearance, long loss, stale input and wrap");
}

int main(int argc, char **argv)
{
    const std::string scenario = argc > 1 ? argv[1] : "normal";
    if (scenario == "course_planner")
    {
        CoursePlanner c;
        c.Reset(UINT32_MAX - 100);
        uint32_t now = UINT32_MAX - 100;
        float d = 0, peak = 0;
        for (unsigned n = 0; n < 14000; ++n)
        {
            now += 5;
            d += c.GetStatus().cruise_rpm * .20106193f / 60 * .005f;
            const auto &s = c.Update(now, d, true, true, false, true);
            assert(!s.invalid && s.cruise_rpm >= 120 && s.cruise_rpm <= 180);
            peak = fmaxf(peak, s.cruise_rpm);
            if (d >= 7)
            {
                assert(s.cruise_rpm < 120.02f);
            }
        }
        assert(peak > 179 && !c.GetStatus().invalid);
        c.Update(now + 50, d, true, true, false, true);
        assert(c.GetStatus().invalid);
        puts("PASS: public course planner, 180 request, slow zones, no 40-second stop, wrap and "
             "stale rejection");
        return 0;
    }
    if (scenario == "finish_gate")
    {
        TestFinishGate();
        return 0;
    }
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
    if (scenario == "course_three_laps" || scenario == "course_three_laps_left" ||
        scenario == "no_time_limit")
    {
        Tick(3000);
        assert(app.GetStatus().phase == LegacyLineRun::Phase::Running);
        if (scenario == "no_time_limit")
        {
            Tick(45000);
            assert(app.GetStatus().phase == LegacyLineRun::Phase::Running);
            app.Stop();
            CheckStopped();
            return 0;
        }
        const bool left = scenario == "course_three_laps_left";
        unsigned stage = 0;
        uint32_t turn_ms = 0;
        bool blank = false;
        float blank_start = 0;
        unsigned dashed = 0;
        bool dash_active = false;
        float dash_start = 0;
        for (unsigned n = 0; n < 25000 && app.GetStatus().phase == LegacyLineRun::Phase::Running;
             ++n)
        {
            const auto &s = line.GetCourseStatus();
            const auto k = line.GetFinishStatus().crossings;
            white_mask = 0x18;
            if (stage == 0)
            {
                const float marker = s.completed_bends == 0 ? 6.8f : 7.2f;
                if (!blank && k % 2 == 0 && s.leg_distance_m >= marker)
                {
                    blank = true;
                    blank_start = s.distance_m;
                }
                if (blank && s.distance_m - blank_start < .06f)
                {
                    white_mask = 0;
                }
                if (blank && line.GetFinishStatus().crossings % 2 == 1)
                {
                    blank = false;
                    stage = 1;
                }
            }
            else if (stage == 1 && s.leg_distance_m >= 7.45f)
            {
                stage = 2;
                turn_ms = test_tick;
            }
            else if (stage == 2)
            {
                white_mask = left ? 0x06 : 0x60;
                if (test_tick - turn_ms >= 4000)
                {
                    stage = 3;
                }
            }
            else if (stage == 3 && s.phase == CoursePlanner::Phase::Low)
            {
                stage = s.completed_bends % 2 ? 4 : 5;
                dashed = 0;
                dash_active = false;
            }
            else if (stage == 4)
            {
                if (dashed < 7 && !dash_active && s.leg_distance_m >= 4.2f + dashed * .15f)
                {
                    dash_active = true;
                    dash_start = s.distance_m;
                }
                if (dash_active)
                {
                    if (s.distance_m - dash_start < .05f)
                    {
                        white_mask = 0;
                    }
                    else
                    {
                        dash_active = false;
                        ++dashed;
                    }
                }
                assert(line.GetFinishStatus().crossings == k && k % 2 == 1);
                if (s.leg_distance_m >= 7.45f)
                {
                    stage = 2;
                    turn_ms = test_tick;
                }
            }
            else if (stage == 5)
            {
                if (!blank && k % 2 == 1 && s.leg_distance_m >= .4f)
                {
                    blank = true;
                    blank_start = s.distance_m;
                }
                if (blank && s.distance_m - blank_start < .06f)
                {
                    white_mask = 0;
                }
                if (blank && line.GetFinishStatus().crossings % 2 == 0)
                {
                    blank = false;
                    stage = 0;
                }
            }
            Tick(10);
            if (app.GetStatus().phase == LegacyLineRun::Phase::Fault)
            {
                printf("course fault%u M%u stage%u K%u B%u S%.2f\n",
                       (unsigned)app.GetStatus().failure, app.GetStatus().port, stage, k,
                       s.completed_bends, s.distance_m);
            }
            assert(app.GetStatus().phase != LegacyLineRun::Phase::Fault);
            for (auto m : motors)
            {
                assert(m->GetTargetSpeed() <= 180 && fabsf(m->GetDuty()) <= .85001f);
            }
        }
        printf("course completed K%u B%u S%.2f\n", line.GetFinishStatus().crossings,
               line.GetCourseStatus().completed_bends, line.GetCourseStatus().distance_m);
        assert(app.GetStatus().phase == LegacyLineRun::Phase::Stopped &&
               line.GetFinishStatus().crossings == 6);
        CheckStopped();
        Tick(8000);
        CheckStopped();
        puts("PASS: three public conventional-PID laps, upper dashed gaps ignored, six markers and "
             "final latch");
        return 0;
    }
    const auto cfg = line.GetTrackConfig();
    assert(cfg.base_rpm == 120 && cfg.kp == 28 && cfg.ki == 0 && cfg.kd == .25f);
    assert(cfg.dual_pid && cfg.turn_kp == 28 && cfg.turn_ki == 0 && cfg.turn_kd == .25f);
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
        const float expected = LegacyLineRun::BaseRpm;
        assert(motors[i]->IsEnabled() && fabsf(motors[i]->GetTargetSpeed() - expected) < .01f);
    }
    if (scenario == "target_bound")
    {
        assert(m1.SetSpeed(180.01f) && m1.GetTargetSpeed() == 180);
        Tick(1);
        assert(app.GetStatus().phase == LegacyLineRun::Phase::Running);
        app.Stop();
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
    return 0;
}
