#include "MainFrame.hpp"
#include "bsp_motor_board.h"
#include "MotorSpeedProfiles.hpp"
#include "LineFollowBoard.hpp"
#include "ultrasonic_factory.hpp"
#include "NovaLink.hpp"
#include "MotorContinuousTest.hpp"

MotorPwmDriver Motor1Driver, Motor2Driver, Motor3Driver, Motor4Driver;
DcMotor Motor1, Motor2, Motor3, Motor4;
MotorProtection MotorSafety(Motor1, Motor2, Motor3, Motor4);
MotorContinuousTest MotorBench(Motor1, Motor2, Motor3, Motor4, MotorSafety);
MotorPwmDriver &LeftMotorDriver = Motor1Driver;
MotorPwmDriver &RightMotorDriver = Motor3Driver;
DcMotor &LeftMotor = Motor1;
DcMotor &RightMotor = Motor3;
DcMotor &LeftFrontMotor = Motor1;
DcMotor &LeftRearMotor = Motor2;
DcMotor &RightFrontMotor = Motor3;
DcMotor &RightRearMotor = Motor4;
Ultrasonic RangeSensor;
GraySensor GrayArray;
OffroadApp Offroad(LeftMotor, RightMotor, RangeSensor, GrayArray);
Led StatusLed;

namespace
{
    DcMotor *const motors[] = {&Motor1, &Motor2, &Motor3, &Motor4};
    MotorPwmDriver *const drivers[] = {&Motor1Driver, &Motor2Driver, &Motor3Driver, &Motor4Driver};

    // M1 左前、M2 左后、M3 右前、M4 右后。
    // 左侧 PWM 反向；M4 的 520 电机正向原始计数实测递增，反馈不取反。
    const bool output_reverse[] = {true, true, false, false};
    const bool encoder_reverse[] = {false, false, true, false};

    bool BindHardware()
    {
        if (BspMotorBoard_Init() != HAL_OK)
        {
            return false;
        }
        for (uint8_t index = 0; index < 4; ++index)
        {
            BspMotorBoard_Port port = {};
            if (BspMotorBoard_GetPort(index + 1U, &port) != HAL_OK)
            {
                return false;
            }
            MotorPwmDriver::Config config;
            config.pwm_timer = port.pwm;
            config.in1_channel = port.in1_channel;
            config.in2_channel = port.in2_channel;
            config.encoder_timer = port.encoder;
            config.counts_per_output_rev = MotorSpeedProfiles::counts_per_output_rev;
            config.output_reverse = output_reverse[index];
            // 此表直接针对 TIM 原始计数，已经包含 M4 的实际 A/B 布线，不再额外异或。
            config.encoder_reverse = encoder_reverse[index];
            // 520 初始 PI 使用实际 PWM，不叠加旧的固定死区补偿。
            config.deadzone_duty = 0;
            config.maximum_duty = MotorSpeedProfiles::duty_limit;
            if (!drivers[index]->Init(config))
            {
                return false;
            }
            if (!motors[index]->Init(drivers[index]->GetDriver()) ||
                !MotorSpeedProfiles::Apply(*motors[index], index + 1U, false))
            {
                return false;
            }
        }
        if (!MotorSafety.Init())
        {
            return false;
        }
        if (MotorContinuousTest::Enabled)
        {
            return true; // Local test configures only its selected motor after binding.
        }
        if (NovaLinkApp::M1TuningEnabled)
        {
            auto pid = Motor1.GetSpeedPidConfig();
            pid.derivative_on_measurement = true;
            if (!Motor1.ConfigureSpeedPid(pid))
            {
                return false;
            }
            return true; // Bench mode has no gray/ultrasonic startup dependency.
        }
        if (!FactoryUltrasonic::Init(RangeSensor))
        {
            System.monitor.LogError("Factory ultrasonic initialization failed");
        }
        if (LineFollowBoard::Enabled)
        {
            LineFollowApp::Config config;
            if (!LineFollowBoard::InitGray(GrayArray) || !LineFollow.Init(config))
            {
                return false;
            }
        }
        // 用户已确认四路单轮测试正常；初始化后保持禁能，比赛启动仍需通过自检。
        // 传感器负责人在这里补充各自驱动绑定。
        // GrayArray.Bind(...);
        // StatusLed.Init(LED_GPIO_Port, LED_Pin, Actuator::Trigger_High);
        // System.monitor.Init(LogSink);
        // TIM6 是 HAL 时间基准，不能用于这些模块。
        return true;
    }

    void StopAll()
    {
        for (auto motor : motors)
        {
            motor->Disable();
        }
    }

    void SetIndicator(bool on)
    {
        if (on)
        {
            StatusLed.On();
        }
        else
        {
            StatusLed.Off();
        }
    }
} // namespace

void MainFrameCpp()
{
    System.BindStopHandler(StopAll);
    System.BindIndicator(SetIndicator);
    if (!MotorContinuousTest::Enabled && !NovaLink.Init())
    {
        System.monitor.LogError("USART1 Bluetooth initialization failed");
    }

    if (!BindHardware())
    {
        System.Stop(true);
        return;
    }
    if (MotorContinuousTest::Enabled)
    {
        System.Stop();
        if (!MotorBench.Init())
        {
            System.Stop(true);
        }
        return;
    }
    if (NovaLinkApp::M1TuningEnabled)
    {
        System.Stop(); // Explicitly idle; only Nova's bounded M1 trial may enable a wheel.
        return;
    }
    Application &app = LineFollowBoard::Enabled ? static_cast<Application &>(LineFollow)
                                                : static_cast<Application &>(Offroad);
    if (!System.RegistApp(app))
    {
        System.Stop(true);
    }
}
