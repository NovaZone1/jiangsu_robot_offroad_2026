#include "fixtures/SyntheticMotorPid.hpp"
#include "MotorProtection.hpp"
#include "MotorSpeedProfiles.hpp"
#include "stm32f1xx_hal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

namespace
{
    struct Feedback
    {
        float rpm = 0;
        float duty = 0;
        bool fresh = false;
        bool disconnected = false;
        bool wrong_direction = false;
        bool stalled = false;
        bool output_failed = false;
    };

    bool Write(void *context, float duty)
    {
        auto &f = *static_cast<Feedback *>(context);
        f.duty = duty;
        return !f.output_failed;
    }

    void Stop(void *context)
    {
        static_cast<Feedback *>(context)->duty = 0;
    }

    bool Read(void *context, float *rpm)
    {
        auto &f = *static_cast<Feedback *>(context);
        if (!f.fresh || f.disconnected)
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
        uint32_t elapsed = 0;
        float drift = 0;
        bool refresh_command = true;

        explicit Rig(uint32_t start = 0) : guard(motors[0], motors[1], motors[2], motors[3])
        {
            test_tick = start;
            for (uint8_t i = 0; i < 4; ++i)
            {
                assert(motors[i].Init({&feedback[i], Write, Stop, Read}));
                assert(MotorSpeedProfiles::Apply(motors[i], i + 1U, false));
                assert(ConfigureSyntheticMotorPid(motors[i]));
                feedback[i].fresh = true;
                motors[i].Control();
            }
            assert(guard.Init());
        }

        void Enable(float rpm = 60)
        {
            for (auto &motor : motors)
            {
                assert(motor.Enable() && motor.SetSpeed(rpm));
            }
        }

        void Step(uint32_t duration)
        {
            for (uint32_t n = 0; n < duration; ++n)
            {
                ++test_tick;
                ++elapsed;
                if (!guard.BeforeControl())
                {
                    continue;
                }
                for (uint8_t i = 0; i < 4; ++i)
                {
                    auto &motor = motors[i];
                    auto &f = feedback[i];
                    if (refresh_command && motor.IsEnabled())
                    {
                        assert(motor.SetSpeed(motor.GetTargetSpeed()));
                    }
                    if (elapsed % 10 == 0)
                    {
                        f.rpm = motor.IsEnabled() ? motor.GetTargetSpeed() : drift;
                        if (f.wrong_direction)
                        {
                            f.rpm = -f.rpm;
                        }
                        if (f.stalled)
                        {
                            f.rpm = 0;
                        }
                        f.fresh = true;
                    }
                    motor.Control();
                }
                guard.AfterControl();
            }
        }

        void Failed(MotorProtection::Fault fault, uint8_t port)
        {
            if (guard.GetStatus().fault != fault || guard.GetStatus().failed_port != port)
            {
                fprintf(stderr, "Protection mismatch at %lu ms: expected %u/%u, actual %u/%u\n",
                        (unsigned long)elapsed, (unsigned)fault, (unsigned)port,
                        (unsigned)guard.GetStatus().fault, (unsigned)guard.GetStatus().failed_port);
            }
            assert(guard.GetStatus().fault == fault && guard.GetStatus().failed_port == port);
            for (uint8_t i = 0; i < 4; ++i)
            {
                assert(!motors[i].IsEnabled() && feedback[i].duty == 0);
            }
        }
    };
} // namespace

void RunMotorProtectionTests()
{
    using Fault = MotorProtection::Fault;
    {
        Rig rig;
        rig.Step(3000); // Idle and zero speed must never start a wheel or report NoMotion.
        assert(rig.guard.GetStatus().fault == Fault::None);
        rig.Enable();
        rig.Step(1800);
        assert(rig.guard.GetStatus().fault == Fault::None);
        for (auto &motor : rig.motors)
        {
            assert(motor.SetSpeed(0));
        }
        rig.Step(1800);
        assert(rig.guard.GetStatus().fault == Fault::None);
    }
    {
        Rig rig(UINT32_MAX - 900);
        rig.Enable(-60);
        rig.Step(1800);
        assert(rig.guard.GetStatus().fault == Fault::None); // Negative RPM and tick rollover.
    }
    {
        Rig rig;
        rig.feedback[3].wrong_direction = true;
        rig.Enable();
        rig.Step(499);
        assert(rig.guard.GetStatus().fault == Fault::None);
        rig.Step(31); // Third fresh wrong-direction sample at 530 ms (10 ms feedback).
        rig.Failed(Fault::WrongDirection, 4);
        rig.Step(100);
        rig.Failed(Fault::WrongDirection, 4); // Failure remains latched.
    }
    {
        Rig rig;
        rig.feedback[1].stalled = true;
        rig.Enable();
        rig.Step(1499);
        assert(rig.guard.GetStatus().fault == Fault::None);
        rig.Step(11);
        rig.Failed(Fault::NoMotion, 2);
    }
    {
        Rig rig;
        rig.Enable();
        rig.Step(100);
        rig.feedback[2].disconnected = true;
        rig.Step(50);
        rig.Failed(Fault::FeedbackLost, 3);
    }
    {
        Rig rig;
        rig.Enable();
        rig.Step(100);
        rig.feedback[0].rpm = 401;
        rig.feedback[0].fresh = true;
        rig.motors[0].Control();
        assert(!rig.guard.AfterControl());
        rig.Failed(Fault::Overspeed, 1);
    }
    {
        Rig rig;
        rig.Enable();
        rig.Step(100);
        test_tick += 50;
        assert(!rig.guard.BeforeControl());
        rig.Failed(Fault::ControlGap, 0);
    }
    {
        Rig rig;
        rig.Enable();
        rig.Step(100);
        rig.feedback[1].output_failed = true;
        rig.Step(10);
        rig.Failed(Fault::MotorFault, 2);
    }
    {
        Rig rig;
        rig.Enable();
        rig.refresh_command = false;
        rig.Step(250);
        rig.Failed(Fault::MotorFault, 1); // Existing command timeout stops all four.
    }
    {
        Rig rig;
        rig.Enable();
        rig.Step(100);
        assert(rig.guard.RequestCoastStop());
        assert(!rig.guard.ReleaseCoastStop());
        rig.Step(999);
        assert(!rig.guard.CanResumeAfterCoast());
        rig.Step(1);
        assert(rig.guard.CanResumeAfterCoast());
        for (auto &motor : rig.motors)
        {
            assert(!motor.IsEnabled()); // No automatic reverse or restart.
        }
        rig.drift = 30;
        rig.Step(10);
        assert(!rig.guard.CanResumeAfterCoast() && !rig.guard.ReleaseCoastStop());
        rig.drift = 0;
        rig.Step(999);
        assert(!rig.guard.CanResumeAfterCoast());
        rig.Step(1);
        assert(rig.guard.CanResumeAfterCoast()); // Require fresh stable feedback again.
        assert(rig.guard.ReleaseCoastStop() && !rig.guard.IsCoasting());
    }
    {
        Rig rig;
        rig.Enable();
        rig.Step(100);
        assert(rig.guard.RequestCoastStop());
        rig.drift = 30;
        rig.Step(2999);
        assert(rig.guard.GetStatus().fault == Fault::None);
        rig.Step(1);
        rig.Failed(Fault::CoastTimeout, 0);
        assert(!rig.guard.ReleaseCoastStop());
    }
    {
        Rig rig;
        MotorProtection duplicate(rig.motors[0], rig.motors[0], rig.motors[2], rig.motors[3]);
        assert(!duplicate.Init());
        assert(duplicate.GetStatus().fault == Fault::NotReady);
    }
    {
        Rig rig(UINT32_MAX - 1500U);
        assert(rig.guard.RequestCoastStop());
        rig.Step(5000);
        assert(rig.guard.CanResumeAfterCoast());
        rig.drift = 30;
        rig.Step(10);
        assert(rig.guard.GetStatus().fault == Fault::None);
        assert(!rig.guard.CanResumeAfterCoast());
        assert(!rig.guard.RequestCoastStop());
        rig.Step(2989);
        assert(rig.guard.GetStatus().fault == Fault::None);
        rig.Step(11);
        rig.Failed(Fault::CoastTimeout, 0);
    }
    puts("PASS: production motor protection, direction/stall/overspeed, timing, feedback, "
         "driver faults, latch, coast interlock and tick rollover");
}
