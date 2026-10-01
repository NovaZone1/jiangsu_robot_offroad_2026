#include "MotorTest.hpp"
#include "stm32f1xx_hal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

namespace
{
    struct Wheel
    {
        float rpm = 0;
        float duty = 0;
        bool fresh = false;
        bool output_failure = false;
    };

    bool Write(void *context, float duty)
    {
        auto &wheel = *static_cast<Wheel *>(context);
        wheel.duty = duty;
        return !wheel.output_failure;
    }

    void Stop(void *context)
    {
        static_cast<Wheel *>(context)->duty = 0;
    }

    bool Read(void *context, float *rpm)
    {
        auto &wheel = *static_cast<Wheel *>(context);
        if (!wheel.fresh)
        {
            return false;
        }
        wheel.fresh = false;
        *rpm = wheel.rpm;
        return true;
    }

    struct Rig
    {
        Wheel wheels[4];
        DcMotor motors[4];
        MotorTest test;

        Rig() : test(motors[0], motors[1], motors[2], motors[3])
        {
            for (unsigned index = 0; index < 4; ++index)
            {
                assert(motors[index].Init({&wheels[index], Write, Stop, Read}));
            }
        }

        void Tick(uint32_t now, bool simulate_motion = true, int missing_port = -1,
                  int wrong_port = -1)
        {
            test_tick = now;
            for (unsigned index = 0; index < 4; ++index)
            {
                // 仅用于代码验证的惯性模型：RPM 向死区之外的控制量 duty*600 靠近。
                if (simulate_motion)
                {
                    const float polarity = (int)index == wrong_port ? -1.0f : 1.0f;
                    wheels[index].rpm +=
                        (wheels[index].duty * 600 * polarity - wheels[index].rpm) * 0.02f;
                }
                wheels[index].fresh = (now % 10U) == 0 && (int)index != missing_port;
            }
            test.Update();
        }

        void AssertStopped()
        {
            for (unsigned index = 0; index < 4; ++index)
            {
                assert(!motors[index].IsEnabled() && wheels[index].duty == 0);
                assert(motors[index].GetTargetSpeed() == 0);
            }
        }
    };
} // namespace

void RunMotorTestSequenceTests()
{
    // 四路真实 DcMotor/PID 配合合成反馈，检查阶段边界及只运行一次。
    {
        test_tick = 0;
        Rig rig;
        assert(rig.test.Start());
        assert(!rig.test.Start());
        for (uint32_t now = 0; now < 12000; ++now)
        {
            rig.Tick(now);
            const auto phase = rig.test.GetStatus().phase;
            if (now < 2000)
            {
                assert(phase == MotorTest::Phase::WaitingForFeedback);
                rig.AssertStopped();
            }
            else
            {
                assert(phase ==
                       (now < 7000 ? MotorTest::Phase::Forward : MotorTest::Phase::Reverse));
                for (auto &motor : rig.motors)
                {
                    assert(motor.IsEnabled());
                    assert(motor.GetTargetSpeed() == (now < 7000 ? 60 : -60));
                    assert(fabsf(motor.GetDuty()) <= 0.15f);
                }
            }
        }
        rig.Tick(12000);
        assert(rig.test.GetStatus().phase == MotorTest::Phase::Finished);
        rig.AssertStopped();
        assert(!rig.test.Start());
        rig.Tick(20000);
        rig.AssertStopped();
    }

    // 输出失败发生在任一路，必须立即禁用其余三路。
    {
        test_tick = 0;
        Rig rig;
        assert(rig.test.Start());
        for (uint32_t now = 0; now <= 2300; ++now)
        {
            rig.Tick(now);
        }
        rig.wheels[2].output_failure = true;
        rig.Tick(2301);
        assert(rig.test.GetStatus().phase == MotorTest::Phase::Failed);
        assert(rig.test.GetStatus().failure == MotorTest::Failure::MotorFault);
        assert(rig.test.GetStatus().failed_port == 3);
        rig.AssertStopped();
    }

    // 新鲜的零 RPM 不能假装轮子已经运动：持续无计数后停止。
    {
        test_tick = 0;
        Rig rig;
        assert(rig.test.Start());
        for (uint32_t now = 0; now <= 3500; ++now)
        {
            rig.Tick(now, false);
        }
        assert(rig.test.GetStatus().failure == MotorTest::Failure::NoMotion);
        rig.AssertStopped();
    }

    // 反馈方向反了，不能让正反馈持续顶着限幅输出。
    {
        test_tick = 0;
        Rig rig;
        assert(rig.test.Start());
        for (uint32_t now = 0; now <= 2600; ++now)
        {
            rig.Tick(now, true, -1, 1);
        }
        assert(rig.test.GetStatus().failure == MotorTest::Failure::WrongDirection);
        assert(rig.test.GetStatus().failed_port == 2);
        rig.AssertStopped();
    }

    // 上电必须有全部四路的新鲜反馈；第三路缺失时不启动其他轮子。
    {
        test_tick = 0;
        Rig rig;
        assert(rig.test.Start());
        for (uint32_t now = 0; now <= 2000; ++now)
        {
            rig.Tick(now, true, 2);
        }
        assert(rig.test.GetStatus().failure == MotorTest::Failure::FeedbackUnavailable);
        assert(rig.test.GetStatus().failed_port == 3);
        rig.AssertStopped();
    }

    // 运行中丢失第四路反馈也要停止全部轮子。
    {
        test_tick = 0;
        Rig rig;
        assert(rig.test.Start());
        for (uint32_t now = 0; now <= 2300; ++now)
        {
            rig.Tick(now);
        }
        for (uint32_t now = 2301; now <= 2360; ++now)
        {
            rig.Tick(now, true, 3);
        }
        assert(rig.test.GetStatus().phase == MotorTest::Phase::Failed);
        assert(rig.test.GetStatus().failed_port == 4);
        rig.AssertStopped();
    }
    // 恢复运行时发现控制循环曾长时间中断，不刷新目标掩盖原先的超时。
    {
        test_tick = 0;
        Rig rig;
        assert(rig.test.Start());
        for (uint32_t now = 0; now <= 2300; ++now)
        {
            rig.Tick(now);
        }
        rig.Tick(2350);
        assert(rig.test.GetStatus().failure == MotorTest::Failure::ControlTimeout);
        rig.AssertStopped();
    }
    puts("PASS: four-wheel PID test, 2s startup/5s forward/5s reverse/one-shot stop and all-wheel "
         "fault stop");
}
