#include "MainFrame.hpp"
#include "bsp_motor_board.h"
#include "bsp_motor_console.h"
#include <stdio.h>

MotorPwmDriver Motor1Driver, Motor2Driver, Motor3Driver, Motor4Driver;
DcMotor Motor1, Motor2, Motor3, Motor4;
MotorPwmDriver &LeftMotorDriver = Motor1Driver;
MotorPwmDriver &RightMotorDriver = Motor3Driver;
DcMotor &LeftMotor = Motor1;
DcMotor &RightMotor = Motor3;
DcMotor &LeftFrontMotor = Motor1;
DcMotor &LeftRearMotor = Motor2;
DcMotor &RightFrontMotor = Motor3;
DcMotor &RightRearMotor = Motor4;
MotorTest MotorBench(Motor1, Motor2, Motor3, Motor4);
Ultrasonic RangeSensor;
GraySensor GrayArray;
OffroadApp Offroad(LeftMotor, RightMotor, RangeSensor, GrayArray);
Led StatusLed;

namespace
{
    DcMotor *const motors[] = {&Motor1, &Motor2, &Motor3, &Motor4};
    MotorPwmDriver *const drivers[] = {&Motor1Driver, &Motor2Driver, &Motor3Driver, &Motor4Driver};

    // M1 左前、M2 左后、M3 右前、M4 右后。
    // 对照官方 car_tracking：左侧 PWM 反向；编码器左侧取正增量、右侧取负增量。
    const bool output_reverse[] = {true, true, false, false};
    const bool encoder_reverse[] = {false, false, true, true};

    void HardwareLog(const char *step, uint8_t port)
    {
#if FRAME_MOTOR_TEST_ENABLED
        char line[80];
        snprintf(line, sizeof(line), "HW step=%s port=%u", step, (unsigned)port);
        (void)BspMotorConsole_WriteLine(line);
#else
        (void)step;
        (void)port;
#endif
    }

    bool BindHardware()
    {
        if (BspMotorBoard_Init() != HAL_OK)
        {
            HardwareLog("BOARD_INIT_FAILED", 0);
            return false;
        }
        for (uint8_t index = 0; index < 4; ++index)
        {
            BspMotorBoard_Port port = {};
            if (BspMotorBoard_GetPort(index + 1U, &port) != HAL_OK)
            {
                HardwareLog("PORT_FAILED", index + 1U);
                return false;
            }
            MotorPwmDriver::Config config;
            config.pwm_timer = port.pwm;
            config.in1_channel = port.in1_channel;
            config.in2_channel = port.in2_channel;
            config.encoder_timer = port.encoder;
            config.output_reverse = output_reverse[index];
            // 此表直接针对 TIM 原始计数，已经包含 M4 的实际 A/B 布线，不再额外异或。
            config.encoder_reverse = encoder_reverse[index];
            config.deadzone_duty = 2000.0f / 3600.0f;
            config.maximum_duty = 0.75f;
            if (!drivers[index]->Init(config))
            {
                HardwareLog("PWM_ENCODER_INIT_FAILED", index + 1U);
                return false;
            }
            if (!motors[index]->Init(drivers[index]->GetDriver()))
            {
                HardwareLog("MODULE_INIT_FAILED", index + 1U);
                return false;
            }
            HardwareLog("READY", index + 1U);
        }
        // 正方向需架空实测；output_reverse/encoder_reverse 分别校正驱动与反馈。
        // 传感器负责人在这里补充各自驱动绑定。
        // RangeSensor.Bind(...);
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
#if FRAME_MOTOR_TEST_ENABLED
    (void)BspMotorConsole_Init();
    (void)BspMotorConsole_WriteLine(
        "BOOT MOTOR_TEST_4W_DZ2000_UART1_115200 waiting=2s forward=5s reverse=5s");
#endif
    System.BindStopHandler(StopAll);
    System.BindIndicator(SetIndicator);

    if (!BindHardware())
    {
        System.Stop(true);
        return;
    }

#if !FRAME_MOTOR_TEST_ENABLED
    if (!System.RegistApp(Offroad))
    {
        System.Stop(true);
    }
#endif
}

void MotorTestLogCpp()
{
#if FRAME_MOTOR_TEST_ENABLED
    static uint32_t last_log = 0;
    static MotorTest::Phase last_phase = MotorTest::Phase::Idle;
    const auto &status = MotorBench.GetStatus();
    const uint32_t now = HAL_GetTick();
    if (status.phase == last_phase && (uint32_t)(now - last_log) < 500U)
    {
        return;
    }
    last_log = now;
    last_phase = status.phase;
    const char *const phases[] = {"IDLE", "WAIT", "FORWARD", "REVERSE", "DONE", "FAILED"};
    const char *const failures[] = {
        "NONE",        "SETUP",           "FEEDBACK",        "ENABLE",
        "MOTOR_FAULT", "CONTROL_TIMEOUT", "WRONG_DIRECTION", "NO_MOTION"};
    uint8_t enabled = 0, online = 0;
    for (uint8_t index = 0; index < 4; ++index)
    {
        enabled |= motors[index]->IsEnabled() ? (1U << index) : 0;
        online |= motors[index]->IsOnline() ? (1U << index) : 0;
    }
    // 用整数打印，避免嵌入式 printf 浮点支持及额外栈开销。
    char line[224];
    snprintf(
        line, sizeof(line),
        "TEST phase=%s failure=%s port=%u ms=%lu en=%u online=%u rpm10=%ld,%ld,%ld,%ld "
        "duty1000=%ld,%ld,%ld,%ld",
        phases[(unsigned)status.phase], failures[(unsigned)status.failure],
        (unsigned)status.failed_port, (unsigned long)status.elapsed_ms, (unsigned)enabled,
        (unsigned)online, (long)(Motor1.GetMeasure().speed_rpm * 10),
        (long)(Motor2.GetMeasure().speed_rpm * 10), (long)(Motor3.GetMeasure().speed_rpm * 10),
        (long)(Motor4.GetMeasure().speed_rpm * 10), (long)(Motor1Driver.GetAppliedDuty() * 1000),
        (long)(Motor2Driver.GetAppliedDuty() * 1000), (long)(Motor3Driver.GetAppliedDuty() * 1000),
        (long)(Motor4Driver.GetAppliedDuty() * 1000));
    (void)BspMotorConsole_WriteLine(line);
#endif
}
