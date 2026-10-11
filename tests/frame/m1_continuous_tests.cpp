#include "MotorContinuousTest.hpp"
#include "MotorSpeedProfiles.hpp"
#include "stm32f1xx_hal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

TestDwt test_dwt = {};
TestCoreDebug test_core_debug = {};
uint32_t test_tick = 0, test_primask = 0;

namespace
{
    struct Feedback
    {
        float rpm = 0, duty = 0;
        bool fresh = false, connected = true, output_ok = true;
    };

    bool Write(void *context, float duty)
    {
        auto &f = *static_cast<Feedback *>(context);
        f.duty = duty;
        return f.output_ok;
    }

    void Stop(void *context)
    {
        static_cast<Feedback *>(context)->duty = 0;
    }

    bool Read(void *context, float *rpm)
    {
        auto &f = *static_cast<Feedback *>(context);
        if (!f.fresh || !f.connected)
        {
            return false;
        }
        f.fresh = false;
        *rpm = f.rpm;
        return true;
    }

    struct Rig
    {
        Feedback feedback[4];
        DcMotor motors[4];
        MotorProtection guard;
        MotorContinuousTest bench;
        bool plant = true;

        Rig()
            : guard(motors[0], motors[1], motors[2], motors[3]),
              bench(motors[0], motors[1], motors[2], motors[3], guard)
        {
            test_tick = 0;
            for (uint8_t i = 0; i < 4; ++i)
            {
                assert(motors[i].Init({&feedback[i], Write, Stop, Read}));
                assert(MotorSpeedProfiles::Apply(motors[i], i + 1, false));
            }
            // Synthetic host model gains only; main firmware defaults remain all zero.
            auto pid = motors[MotorContinuousTest::SelectedIndex].GetSpeedPidConfig();
            pid.kp = .002f;
            pid.ki = .012f;
            assert(motors[MotorContinuousTest::SelectedIndex].ConfigureSpeedPid(pid));
            assert(guard.Init());
            assert(bench.Init());
        }

        void Step(uint32_t duration)
        {
            while (duration--)
            {
                ++test_tick;
                if (plant)
                {
                    auto &f = feedback[MotorContinuousTest::SelectedIndex];
                    f.rpm += (400 * f.duty - f.rpm) * 0.001f / 0.18f;
                }
                for (auto &f : feedback)
                {
                    f.fresh = test_tick % 10 == 0;
                }
                bench.Run();
                assert(fabsf(feedback[MotorContinuousTest::SelectedIndex].duty) <= 0.69001f);
                for (uint8_t i = 0; i < 4; ++i)
                {
                    if (i != MotorContinuousTest::SelectedIndex)
                    {
                        assert(!motors[i].IsEnabled() && feedback[i].duty == 0);
                    }
                }
            }
        }

        void ExpectFault(MotorProtection::Fault fault)
        {
            assert(guard.GetStatus().fault == fault);
            assert(bench.GetStatus().phase == MotorContinuousTest::Phase::Fault);
            Step(5000);
            for (auto &motor : motors)
            {
                assert(!motor.IsEnabled() && motor.GetDuty() == 0);
            }
            assert(!bench.Init()); // Fault cannot be cleared by reinitializing this test.
        }
    };
} // namespace

int main()
{
    {
        Rig r;
        r.Step(1999);
        assert(!r.motors[MotorContinuousTest::SelectedIndex].IsEnabled());
        r.Step(1);
        assert(r.motors[MotorContinuousTest::SelectedIndex].IsEnabled() &&
               r.motors[MotorContinuousTest::SelectedIndex].GetTargetSpeed() == 100);
        r.Step(60000); // Local source runs beyond Nova's trial cap without a serial heartbeat.
        assert(r.bench.GetStatus().phase == MotorContinuousTest::Phase::Running);
        assert(fabsf(r.feedback[MotorContinuousTest::SelectedIndex].rpm - 100) < 2);
        r.bench.Stop();
        r.Step(5000);
        assert(!r.motors[MotorContinuousTest::SelectedIndex].IsEnabled());
    }
    {
        Rig r;
        r.plant = false;
        r.Step(3600);
        assert(r.guard.GetStatus().failed_port == MotorContinuousTest::SelectedPort);
        assert(fabsf(r.bench.GetStatus().duty - 0.69f) < 0.00001f);
        r.ExpectFault(MotorProtection::Fault::NoMotion);
    }
    {
        Rig r;
        r.Step(2100);
        test_tick += 50;
        r.Step(1);
        r.ExpectFault(MotorProtection::Fault::ControlGap);
    }
    {
        Rig r;
        r.Step(2200);
        r.feedback[MotorContinuousTest::SelectedIndex].connected = false;
        r.Step(100);
        r.ExpectFault(MotorProtection::Fault::FeedbackLost);
    }
    {
        Rig r;
        r.Step(2100);
        r.plant = false;
        r.feedback[MotorContinuousTest::SelectedIndex].rpm = -60;
        r.Step(500);
        r.ExpectFault(MotorProtection::Fault::WrongDirection);
    }
    {
        Rig r;
        r.Step(2100);
        r.plant = false;
        r.feedback[MotorContinuousTest::SelectedIndex].rpm = 401;
        r.Step(10);
        r.ExpectFault(MotorProtection::Fault::Overspeed);
    }
    {
        Rig r;
        r.Step(2100);
        r.feedback[MotorContinuousTest::SelectedIndex].output_ok = false;
        r.Step(10);
        r.ExpectFault(MotorProtection::Fault::MotorFault);
    }
    {
        Rig r;
        r.Step(2100);
        r.motors[MotorContinuousTest::SelectedIndex].Disable();
        r.Step(1000);
        assert(r.bench.GetStatus().failure == MotorContinuousTest::Failure::UnexpectedDisable);
        assert(!r.motors[MotorContinuousTest::SelectedIndex].IsEnabled());
    }
    {
        Rig r;
        r.plant = false;
        r.feedback[MotorContinuousTest::SelectedIndex].rpm = 10;
        r.Step(3200);
        r.ExpectFault(MotorProtection::Fault::CoastTimeout);
    }
    printf(
        "M%u continuous: startup, 60 s run, exclusive wheel control and protection tests passed\n",
        static_cast<unsigned>(MotorContinuousTest::SelectedPort));
}
