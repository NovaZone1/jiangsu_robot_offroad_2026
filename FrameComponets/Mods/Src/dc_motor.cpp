#include "dc_motor.hpp"
#include <math.h>

bool DcMotor::Bind(const Driver &driver)
{
    if (!driver.set_duty || !driver.stop)
    {
        return false;
    }
    Stop();
    driver_ = driver;
    Stop();
    return true;
}

bool DcMotor::SetDuty(float duty)
{
    if (!IsReady() || !isfinite(duty))
    {
        Stop();
        return false;
    }
    if (duty > 1)
    {
        duty = 1;
    }
    if (duty < -1)
    {
        duty = -1;
    }
    if (!driver_.set_duty(driver_.context, duty))
    {
        Stop();
        return false;
    }
    duty_ = duty;
    return true;
}

void DcMotor::Stop()
{
    duty_ = 0;
    if (driver_.stop)
    {
        driver_.stop(driver_.context);
    }
}

bool DcMotor::ReadRpm(float &rpm)
{
    float sample = 0;
    if (!driver_.read_rpm || !driver_.read_rpm(driver_.context, &sample) || !isfinite(sample))
    {
        return false;
    }
    rpm = sample;
    return true;
}

/* TODO: implement H-bridge direction/PWM and encoder-to-RPM adapter here.
 * Candidate BSPs: bsp_tim_pwm, bsp_gpio, bsp_encoder, pid.
 * Define reversal dead time, wheel encoder CPR and reduction ratio from the
 * actual hardware. No PWM is started by this placeholder module. */
