#include "motor_pwm_driver.hpp"
#include "MotorSpeedProfiles.hpp"
#include <assert.h>
#include <math.h>
#include <limits>
#include <stdio.h>

TestRcc test_rcc = {};
TIM_TypeDef test_tim1 = {}, test_tim2 = {}, test_tim3 = {}, test_tim4 = {}, test_tim5 = {},
            test_tim8 = {};
int test_pwm_start_fail_channel = -1;
bool test_encoder_start_failure = false;

namespace
{
    void ResetPwm(TIM_TypeDef &timer)
    {
        timer = {};
        timer.ARR = 3599;
        timer.CCMR1 = timer.CCMR2 = (6U << 4U) | (6U << 12U);
    }

    struct Feedback
    {
        float rpm = 0;
        float duty = 0;
        bool fresh = false;
        bool fail = false;
    };

    bool Write(void *context, float duty)
    {
        auto &feedback = *static_cast<Feedback *>(context);
        feedback.duty = duty;
        return !feedback.fail;
    }

    void Stop(void *context)
    {
        static_cast<Feedback *>(context)->duty = 0;
    }

    bool Read(void *context, float *rpm)
    {
        auto &feedback = *static_cast<Feedback *>(context);
        if (!feedback.fresh)
        {
            return false;
        }
        feedback.fresh = false;
        *rpm = feedback.rpm;
        return true;
    }
} // namespace

void RunMotorTests()
{
    // 实际双 PWM 驱动：初始化为零，正反转间必须存在滑行区间。
    ResetPwm(test_tim8);
    test_tim4 = {};
    test_tim4.ARR = 65535;
    test_tim4.CNT = 65530;
    TIM_HandleTypeDef pwm = {TIM8}, encoder = {TIM4};
    MotorPwmDriver driver;
    MotorPwmDriver::Config config;
    config.pwm_timer = &pwm;
    config.encoder_timer = &encoder;
    test_tick = 0;
    assert(driver.Init(config));
    assert(!driver.Init(config)); // 禁止运行期间重新注册同一驱动
    auto callbacks = driver.GetDriver();
    assert(test_tim8.CCR[0] == 0 && test_tim8.CCR[1] == 0 && test_tim8.started == 3);
    assert(callbacks.set_duty(callbacks.context, 0.5f));
    assert(test_tim8.CCR[0] == 1800 && test_tim8.CCR[1] == 0);
    assert(driver.GetAppliedDuty() == 0.5f);
    test_tim8.EGR = 0;
    assert(callbacks.set_duty(callbacks.context, 0.5f) && test_tim8.EGR == 0);
    test_tick = 1;
    assert(callbacks.set_duty(callbacks.context, -0.25f));
    assert(test_tim8.CCR[0] == 0 && test_tim8.CCR[1] == 0);
    test_tick = 2;
    assert(callbacks.set_duty(callbacks.context, -0.25f));
    assert(test_tim8.CCR[1] == 0);
    test_tick = 3;
    assert(callbacks.set_duty(callbacks.context, -0.25f));
    assert(test_tim8.CCR[0] == 0 && test_tim8.CCR[1] == 900);
    callbacks.stop(callbacks.context);
    assert(test_tim8.CCR[0] == 0 && test_tim8.CCR[1] == 0);
    assert(driver.GetAppliedDuty() == 0);

    // 编码器跨回绕与输出轴 RPM：十毫秒内 10 个计数 = 57.6923 RPM。
    float rpm = 123;
    test_tick = 5;
    test_tim4.CNT = 65535;
    assert(!callbacks.read_rpm(callbacks.context, &rpm));
    test_tick = 10;
    test_tim4.CNT = 4;
    assert(callbacks.read_rpm(callbacks.context, &rpm));
    assert(fabsf(rpm - 60000.0f / 1040.0f) < 0.001f && driver.GetEncoderCount() == 10);
    assert(!callbacks.read_rpm(callbacks.context, &rpm)); // 不重复交付旧样本
    test_tick = 20;
    test_tim4.CNT = 65530;
    assert(callbacks.read_rpm(callbacks.context, &rpm));
    assert(rpm < 0 && driver.GetEncoderCount() == 0);

    // 重复声明同一通道或编码器不能清零正在运行的其他驱动。
    assert(callbacks.set_duty(callbacks.context, 0.3f));
    MotorPwmDriver duplicate;
    assert(!duplicate.Init(config) && test_tim8.CCR[0] == 1080);
    auto shared_config = config;
    shared_config.in1_channel = TIM_CHANNEL_3;
    shared_config.in2_channel = TIM_CHANNEL_4;
    assert(!duplicate.Init(shared_config) && test_tim8.CCR[0] == 1080);
    shared_config.encoder_timer = nullptr;
    {
        MotorPwmDriver second_pair;
        assert(second_pair.Init(shared_config));
        auto second_callbacks = second_pair.GetDriver();
        assert(second_callbacks.set_duty(second_callbacks.context, 0.2f));
        assert(test_tim8.CCR[0] == 1080 && test_tim8.CCR[2] == 720);
    }
    assert(test_tim8.CCR[0] == 1080 && test_tim8.CCR[2] == 0 && test_tim8.started == 3);

    // 只修改第二组通道，不损坏同一定时器的第一组。
    test_tim8.CCR[0] = 100;
    test_tim8.CCR[1] = 200;
    BspTIMPWM_TypeDef third = {}, fourth = {};
    assert(BspTIMPWM_Init(&third, &pwm, TIM_CHANNEL_3) == HAL_OK);
    assert(BspTIMPWM_Init(&fourth, &pwm, TIM_CHANNEL_4) == HAL_OK);
    test_primask = 1;
    assert(BspTIMPWM_WritePair(&third, 1, &fourth, 0) == HAL_OK);
    assert(test_primask == 1 && test_tim8.CCR[0] == 100 && test_tim8.CCR[1] == 200);
    assert(test_tim8.CCR[2] == 3600 && test_tim8.EGR == TIM_EGR_UG);
    test_primask = 0;
    assert(BspTIMPWM_WriteDuty(&third, std::numeric_limits<float>::quiet_NaN()) == HAL_ERROR);
    assert(BspTIMPWM_WritePair(&third, 1, &third, 0) == HAL_ERROR);
    driver.DeInit();
    assert(!driver.IsReady() && test_tim8.started == 0);
    assert(!callbacks.set_duty(callbacks.context, 1));

    // 释放后可重绑；输出极性和反馈方向分别配置，换向间隔支持 tick 回绕。
    config.output_reverse = true;
    config.encoder_reverse = true;
    test_tick = 0xFFFFFFFEU;
    assert(driver.Init(config));
    callbacks = driver.GetDriver();
    assert(callbacks.set_duty(callbacks.context, 0.5f));
    assert(test_tim8.CCR[0] == 0 && test_tim8.CCR[1] == 1800);
    assert(callbacks.set_duty(callbacks.context, -0.5f));
    test_tick = 0;
    assert(callbacks.set_duty(callbacks.context, -0.5f));
    assert(test_tim8.CCR[0] == 1800 && test_tim8.CCR[1] == 0);
    test_tick = 8;
    test_tim4.CNT = 4;
    assert(callbacks.read_rpm(callbacks.context, &rpm) && rpm < 0);
    driver.DeInit();
    config.output_reverse = config.encoder_reverse = false;

    // 官方 2000/3600 死区补偿：零仍停止，补偿只作用于非零指令，实际输出单独限幅。
    {
        MotorPwmDriver compensated;
        auto calibrated = config;
        calibrated.encoder_timer = nullptr;
        calibrated.deadzone_duty = 2000.0f / 3600.0f;
        calibrated.maximum_duty = 0.75f;
        calibrated.output_reverse = true;
        assert(compensated.Init(calibrated));
        auto output = compensated.GetDriver();
        assert(output.set_duty(output.context, 0));
        assert(test_tim8.CCR[0] == 0 && test_tim8.CCR[1] == 0);
        assert(output.set_duty(output.context, 0.1f));
        assert(test_tim8.CCR[0] == 0 && test_tim8.CCR[1] == 2360);
        assert(fabsf(compensated.GetAppliedDuty() - (0.1f + calibrated.deadzone_duty)) < 0.001f);
        assert(output.set_duty(output.context, 1));
        assert(test_tim8.CCR[1] == 2700 && compensated.GetAppliedDuty() == 0.75f);
        output.stop(output.context);
        assert(test_tim8.CCR[0] == 0 && test_tim8.CCR[1] == 0);
        assert(output.set_duty(output.context, -0.1f));
        test_tick += 2;
        assert(output.set_duty(output.context, -0.1f));
        assert(test_tim8.CCR[0] == 2360 && test_tim8.CCR[1] == 0);
        compensated.DeInit();
        calibrated.maximum_duty = calibrated.deadzone_duty;
        assert(!compensated.Init(calibrated));
    }

    // 初始化失败不能留下运行通道；错误 PWM 极性和过大的 ARR 被拒绝。
    ResetPwm(test_tim1);
    TIM_HandleTypeDef bad_pwm = {TIM1};
    config.pwm_timer = &bad_pwm;
    config.encoder_timer = nullptr;
    MotorPwmDriver failed;
    test_pwm_start_fail_channel = TIM_CHANNEL_2;
    assert(!failed.Init(config));
    assert(test_tim1.started == 0 && test_tim1.CCR[0] == 0 && test_tim1.CCR[1] == 0);
    test_pwm_start_fail_channel = -1;
    test_tim1.CCER = TIM_CCER_CC1P;
    assert(!failed.Init(config));
    test_tim1.CCER = 0;
    test_tim1.CCMR1 |= 1U; // 输入捕获通道不能伪装成 PWM。
    assert(!failed.Init(config));
    ResetPwm(test_tim1);
    test_tim1.ARR = 65535;
    assert(!failed.Init(config));
    ResetPwm(test_tim1);
    config.encoder_timer = &encoder;
    test_encoder_start_failure = true;
    assert(!failed.Init(config));
    assert(test_tim1.started == 0);
    test_encoder_start_failure = false;

    // 非 65535 的 ARR、半周期方向歧义、编码器启动失败。
    test_tim2 = {};
    test_tim2.ARR = 9;
    test_tim2.CNT = 8;
    TIM_HandleTypeDef small_encoder = {TIM2};
    BspEncoder_Instance counter = {};
    assert(BspEncoder_Init(&counter, &small_encoder) == HAL_OK);
    int32_t delta = 0;
    test_tim2.CNT = 1;
    assert(BspEncoder_SampleChecked(&counter, &delta) == HAL_OK && delta == 3);
    test_tim2.CNT = 6;
    assert(BspEncoder_SampleChecked(&counter, &delta) == HAL_ERROR);

    // API 默认禁能；非法指令、HAL 输出错误和命令超时均清除目标并停机。
    Feedback feedback;
    DcMotor motor;
    assert(motor.Init({&feedback, Write, Stop, Read}));
    assert(!motor.IsEnabled() && !motor.SetDuty(1));
    assert(motor.Enable());
    assert(motor.DutyLimSet(0.4f));
    assert(motor.SetDuty(1) && feedback.duty == 0.4f);
    motor.Neutral();
    assert(motor.IsEnabled() && motor.GetDuty() == 0);
    motor.Disable();
    assert(!motor.IsEnabled() && motor.SetTimeouts(20, 30));
    assert(motor.Enable());
    test_tick = 0xFFFFFFF5U;
    assert(motor.SetDuty(0.2f));
    test_tick = 9;
    motor.Control();
    assert(!motor.IsEnabled() && motor.GetFault() == DcMotor::Fault::CommandTimeout);
    assert(feedback.duty == 0);
    assert(motor.ClearFault() && motor.Enable());
    feedback.fail = true;
    assert(!motor.SetDuty(0.1f) && motor.GetFault() == DcMotor::Fault::OutputFailure);
    feedback.fail = false;
    assert(motor.ClearFault());

    // PID 只用合成反馈验证程序，不提供任何实车调参值。
    assert(motor.SwitchMode(DcMotor::Mode::Speed));
    assert(!motor.Enable()); // 没有配置 PID/速度上限/有效反馈
    DcMotor::SpeedPidConfig pid;
    assert(motor.ConfigureSpeedPid(pid) && !motor.Enable()); // 全零增益禁止闭环
    pid.kp = 0.01f;
    pid.ki = 1;
    assert(motor.ConfigureSpeedPid(pid));
    assert(motor.SpeedLimSet(200));
    assert(motor.SetTimeouts(100, 30));
    test_tick = 100;
    feedback.fresh = true;
    assert(motor.Enable());
    assert(motor.SetSpeed(999) && motor.GetTargetSpeed() == 200);
    assert(motor.SetSpeed(100));
    test_tick = 110;
    feedback.fresh = true;
    motor.Control();
    assert(feedback.duty == 0.4f);
    feedback.rpm = 100;
    feedback.fresh = true;
    test_tick = 120;
    float actual = 0;
    assert(motor.ReadRpm(actual) && actual == 100);
    motor.Control(); // ReadRpm 消费过的样本仍应推进一次 PID
    assert(fabsf(feedback.duty) < 0.001f); // 饱和时未累计积分
    test_tick = 150;
    motor.Control();
    assert(motor.GetFault() == DcMotor::Fault::FeedbackLost && feedback.duty == 0);
    assert(motor.ClearFault());
    test_tick = 200;
    feedback.fresh = true;
    assert(motor.Enable());
    assert(motor.SetSpeed(100));
    feedback.rpm = std::numeric_limits<float>::quiet_NaN();
    feedback.fresh = true;
    motor.Control();
    assert(motor.GetFault() == DcMotor::Fault::FeedbackLost && !motor.IsEnabled());
    assert(motor.ClearFault());
    assert(motor.SwitchMode(DcMotor::Mode::Duty) && motor.Enable());
    assert(!motor.SetDuty(std::numeric_limits<float>::infinity()));
    assert(motor.GetFault() == DcMotor::Fault::InvalidCommand && feedback.duty == 0);
    assert(motor.Init({&feedback, Write, Stop, Read}, DcMotor::Mode::Speed));
    feedback.fresh = true;
    assert(!motor.Enable()); // 重新绑定后不沿用原电机的 PID 和速度上限。

    // 被销毁的临时对象必须退出批量控制注册表。
    {
        DcMotor temporary;
        assert(temporary.Bind({&feedback, Write, Stop, nullptr}));
    }
    DcMotor::ControlAllMotors();

    // 正式板级参数只允许禁能配置；滤波保留原始 RPM，无新样本不推进滤波。
    motor.Disable();
    assert(motor.ClearFault());
    assert(!MotorSpeedProfiles::Apply(motor, 0, false));
    assert(!MotorSpeedProfiles::Apply(motor, 5, false));
    assert(MotorSpeedProfiles::Apply(motor, 2, false));
    assert(!motor.IsEnabled() && motor.GetMode() == DcMotor::Mode::Speed);
    test_tick = 2000;
    feedback.rpm = 0;
    feedback.fresh = true;
    motor.Control();
    assert(motor.Enable() && motor.SetSpeed(60));
    assert(!MotorSpeedProfiles::Apply(motor, 2, true));
    test_tick = 2010;
    feedback.rpm = 40;
    feedback.fresh = true;
    motor.Control();
    assert(motor.GetMeasure().speed_rpm == 40);
    assert(fabsf(motor.GetFilteredRpm() - 10) < 0.001f);
    test_tick = 2020;
    motor.Control();
    assert(fabsf(motor.GetFilteredRpm() - 10) < 0.001f);
    motor.Disable();
    assert(MotorSpeedProfiles::Apply(motor, 2, true));
    test_tick = 2030;
    feedback.fresh = true;
    motor.Control();
    assert(motor.GetFilteredRpm() == 40 && !motor.IsEnabled());

    puts("PASS: AT8236 PWM, reversal delay, encoder/RPM rollover, HAL failures, motor API and "
         "speed PID guards");
}
