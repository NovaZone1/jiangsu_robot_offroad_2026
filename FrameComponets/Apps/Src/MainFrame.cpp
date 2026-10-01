#include "MainFrame.hpp"

DcMotor LeftMotor;
DcMotor RightMotor;
Ultrasonic RangeSensor;
GraySensor GrayArray;
OffroadApp Offroad(LeftMotor, RightMotor, RangeSensor, GrayArray);
Led StatusLed;

namespace
{
    void BindHardware()
    {
        // TODO：在 CubeMX 外设初始化后绑定实际驱动和引脚。
        // LeftMotor.Bind(...);
        // RightMotor.Bind(...);
        // RangeSensor.Bind(...);
        // GrayArray.Bind(...);
        // StatusLed.Init(LED_GPIO_Port, LED_Pin, Actuator::Trigger_High);
        // System.monitor.Init(LogSink);
        // TIM6 是 HAL 时间基准，不能用于这些模块。
    }

    void StopAll()
    {
        Offroad.StopMotors();
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
    BindHardware();
    System.BindStopHandler(StopAll);
    System.BindIndicator(SetIndicator);

    if (!System.RegistApp(Offroad))
    {
        System.Stop(true);
    }
}