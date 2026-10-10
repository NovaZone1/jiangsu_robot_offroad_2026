#include "Motor520Test.hpp"
#include "MotorSpeedProfiles.hpp"
#include "motor_pwm_driver.hpp"
#include "stm32f1xx_hal.h"
#include <assert.h>
#include <math.h>
#include <initializer_list>
#include <stdio.h>

namespace
{
    struct Feedback
    {
        float rpm = 0;
        float duty = 0;
        bool fresh = false;
        bool disconnected = false;
        bool reverse = false;
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
        Motor520Test test;
        uint32_t elapsed = 0;
        bool drifting = false;

        explicit Rig(uint32_t start = 0, uint8_t ports = 0x0F)
            : test(motors[0], motors[1], motors[2], motors[3], ports)
        {
            test_tick = start;
            for (uint8_t i = 0; i < 4; ++i)
            {
                assert(motors[i].Init({&feedback[i], Write, Stop, Read}));
                assert(MotorSpeedProfiles::Apply(motors[i], i + 1U, false));
            }
            assert(test.Start());
        }

        void Step(uint32_t duration = 1)
        {
            for (uint32_t n = 0; n < duration; ++n)
            {
                ++test_tick;
                ++elapsed;
                if (elapsed % 10 == 0)
                {
                    for (uint8_t i = 0; i < 4; ++i)
                    {
                        auto &f = feedback[i];
                        // 合成测速用于阶段与保护验证，不代表实车响应。
                        f.rpm = motors[i].GetTargetSpeed();
                        if (drifting && test.GetStatus().phase == Motor520Test::Phase::Coasting)
                        {
                            f.rpm = 30;
                        }
                        if (f.reverse)
                        {
                            f.rpm = -f.rpm;
                        }
                        if (f.stalled)
                        {
                            f.rpm = 0;
                        }
                        f.fresh = true;
                    }
                }
                test.Update();
            }
        }

        void Expect(Motor520Test::Phase phase, float target)
        {
            assert(test.GetStatus().phase == phase);
            for (uint8_t i = 0; i < 4; ++i)
            {
                const bool selected = (test.GetStatus().port_mask & (1U << i)) != 0;
                assert(motors[i].GetTargetSpeed() == (selected ? target : 0));
                assert(motors[i].IsEnabled() == (selected && target != 0));
                if (!selected)
                {
                    assert(feedback[i].duty == 0);
                }
            }
        }

        void Failed(Motor520Test::Failure failure, uint8_t port)
        {
            Expect(Motor520Test::Phase::Failed, 0);
            assert(test.GetStatus().failure == failure && test.GetStatus().failed_port == port);
            for (const auto &f : feedback)
            {
                assert(f.duty == 0);
            }
            assert(!test.Start());
        }
    };
} // namespace

void RunMotor520Tests()
{
    // 使用真实适配器，验证 1320 CPR 正反转及两个方向的计数回绕。
    {
        test_tim8 = {};
        test_tim8.ARR = 3599;
        test_tim8.CCMR1 = test_tim8.CCMR2 = (6U << 4U) | (6U << 12U);
        test_tim4 = {};
        test_tim4.ARR = 65535;
        test_tim4.CNT = 65530;
        TIM_HandleTypeDef pwm = {TIM8}, encoder = {TIM4};
        MotorPwmDriver driver;
        MotorPwmDriver::Config config;
        config.pwm_timer = &pwm;
        config.encoder_timer = &encoder;
        config.counts_per_output_rev = MotorSpeedProfiles::counts_per_output_rev;
        assert(config.counts_per_output_rev == 1320);
        test_tick = 0;
        assert(driver.Init(config));
        const auto cb = driver.GetDriver();
        float rpm = 0;
        test_tick = 10;
        test_tim4.CNT = 16; // +22 计数 / 10 ms = 100 RPM。
        assert(cb.read_rpm(cb.context, &rpm) && fabsf(rpm - 100) < 0.001f);
        test_tick = 20;
        test_tim4.CNT = 65530;
        assert(cb.read_rpm(cb.context, &rpm) && fabsf(rpm + 100) < 0.001f);
    }
    // 四路完整序列、5 秒边界、换向禁能和最终单次停止。
    for (const uint32_t start : {0U, 0xFFFFF800U})
    {
        Rig rig(start);
        rig.Step(1999);
        rig.Expect(Motor520Test::Phase::Waiting, 0);
        rig.Step();
        rig.Expect(Motor520Test::Phase::ForwardSlow, 60);
        rig.Step(4999);
        rig.Expect(Motor520Test::Phase::ForwardSlow, 60);
        rig.Step();
        rig.Expect(Motor520Test::Phase::ForwardFast, 120);
        rig.Step(5000);
        rig.Expect(Motor520Test::Phase::Coasting, 0);
        rig.Step(999);
        rig.Expect(Motor520Test::Phase::Coasting, 0);
        rig.Step();
        rig.Expect(Motor520Test::Phase::ReverseSlow, -60);
        rig.Step();
        rig.Expect(Motor520Test::Phase::ReverseSlow, -60);
        rig.Step(4999);
        rig.Expect(Motor520Test::Phase::ReverseFast, -120);
        rig.Step(4999);
        rig.Expect(Motor520Test::Phase::ReverseFast, -120);
        rig.Step();
        rig.Expect(Motor520Test::Phase::Finished, 0);
        assert(!rig.test.Start());
        rig.Step(10000);
        rig.Expect(Motor520Test::Phase::Finished, 0);
    }
    {
        Rig rig;
        rig.Step(2100);
        rig.feedback[3].reverse = true;
        rig.Step(500);
        rig.Failed(Motor520Test::Failure::WrongDirection, 4);
    }
    {
        Rig rig;
        rig.Step(2100);
        rig.feedback[1].disconnected = true;
        rig.Step(50);
        rig.Failed(Motor520Test::Failure::FeedbackLost, 2);
    }
    {
        Rig rig;
        rig.feedback[2].stalled = true;
        rig.Step(3500);
        rig.Failed(Motor520Test::Failure::NoMotion, 3);
    }
    {
        Rig rig;
        rig.Step(2100);
        test_tick += 50;
        rig.test.Update();
        rig.Failed(Motor520Test::Failure::ControlGap, 0);
    }
    {
        Rig rig;
        rig.Step(2100);
        rig.feedback[0].output_failed = true;
        rig.Step();
        rig.Failed(Motor520Test::Failure::MotorFault, 1);
    }
    {
        Rig rig;
        rig.drifting = true;
        rig.Step(15000);
        rig.Failed(Motor520Test::Failure::CoastTimeout, 0);
    }
    // 复现原现象：四轮入口中只有 M2 有计数，1.5 秒后 M1 的无转速保护停车。
    {
        Rig rig;
        rig.feedback[0].stalled = rig.feedback[2].stalled = rig.feedback[3].stalled = true;
        rig.Step(3499);
        rig.Expect(Motor520Test::Phase::ForwardSlow, 60);
        rig.Step();
        rig.Failed(Motor520Test::Failure::NoMotion, 1);
    }
    // 逐个选择 M1～M4：其余三路无反馈不阻断流程，始终不输出 PWM。
    for (const uint8_t port : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3)})
    {
        Rig rig(0, uint8_t(1U << port));
        for (uint8_t i = 0; i < 4; ++i)
        {
            rig.feedback[i].disconnected = i != port;
        }
        rig.Step(2000);
        rig.Expect(Motor520Test::Phase::ForwardSlow, 60);
        rig.Step(5000);
        rig.Expect(Motor520Test::Phase::ForwardFast, 120);
        rig.Step(5000);
        rig.Expect(Motor520Test::Phase::Coasting, 0);
        rig.Step(1000);
        rig.Expect(Motor520Test::Phase::ReverseSlow, -60);
        rig.Step(5000);
        rig.Expect(Motor520Test::Phase::ReverseFast, -120);
        rig.Step(5000);
        rig.Expect(Motor520Test::Phase::Finished, 0);
        assert(rig.test.GetStatus().failure == Motor520Test::Failure::None);
    }
    // 各端口单轮试车仍有反馈保护，不能借端口选择绕过。
    for (const uint8_t port : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3)})
    {
        Rig rig(0, uint8_t(1U << port));
        rig.Step(2100);
        rig.feedback[port].disconnected = true;
        rig.Step(50);
        rig.Failed(Motor520Test::Failure::FeedbackLost, port + 1U);
    }
    {
        DcMotor motors[4];
        Motor520Test empty(motors[0], motors[1], motors[2], motors[3], 0);
        Motor520Test out_of_range(motors[0], motors[1], motors[2], motors[3], 0x10);
        assert(!empty.Start() && !out_of_range.Start());
        assert(empty.GetStatus().failure == Motor520Test::Failure::InvalidPorts);
        assert(out_of_range.GetStatus().failure == Motor520Test::Failure::InvalidPorts);
    }
    puts("PASS: 520 CPR, four-wheel closed-loop stages, tick rollover, one-shot stop and faults");
}
