#include "motor_pwm_driver.hpp"

#include <math.h>

namespace
{
    bool IsActiveHighPwm1(TIM_HandleTypeDef *timer, uint32_t channel)
    {
        if (!timer || !timer->Instance || !IS_TIM_CCX_INSTANCE(timer->Instance, channel))
        {
            return false;
        }
        const bool second = channel == TIM_CHANNEL_2 || channel == TIM_CHANNEL_4;
        const uint32_t ccmr =
            channel <= TIM_CHANNEL_2 ? timer->Instance->CCMR1 : timer->Instance->CCMR2;
        const uint32_t shift = second ? 8U : 0U;
        return ((ccmr >> shift) & 3U) == 0 && ((ccmr >> (shift + 4U)) & 7U) == 6U &&
               (timer->Instance->CCER & (TIM_CCER_CC1P << channel)) == 0;
    }
} // namespace

MotorPwmDriver *MotorPwmDriver::owners_[FRAME_MAX_MOTORS] = {};

MotorPwmDriver::~MotorPwmDriver()
{
    DeInit();
}

bool MotorPwmDriver::ClaimResources()
{
    for (auto owner : owners_)
    {
        if (!owner)
        {
            continue;
        }
        const auto &other = owner->config_;
        if ((config_.encoder_timer &&
             (config_.encoder_timer->Instance == other.pwm_timer->Instance ||
              (other.encoder_timer &&
               config_.encoder_timer->Instance == other.encoder_timer->Instance))) ||
            (other.encoder_timer && config_.pwm_timer->Instance == other.encoder_timer->Instance))
        {
            return false;
        }
        if (config_.pwm_timer->Instance == other.pwm_timer->Instance &&
            (config_.in1_channel == other.in1_channel || config_.in1_channel == other.in2_channel ||
             config_.in2_channel == other.in1_channel || config_.in2_channel == other.in2_channel))
        {
            return false;
        }
    }
    for (auto &owner : owners_)
    {
        if (!owner)
        {
            owner = this;
            resources_claimed_ = true;
            return true;
        }
    }
    return false;
}

void MotorPwmDriver::ReleaseResources()
{
    for (auto &owner : owners_)
    {
        if (owner == this)
        {
            owner = nullptr;
        }
    }
    resources_claimed_ = false;
}

void MotorPwmDriver::DeInit()
{
    if (resources_claimed_)
    {
        Fail();
        ReleaseResources();
    }
    in1_ = {};
    in2_ = {};
    encoder_ = {};
    last_direction_ = 0;
}

bool MotorPwmDriver::Init(const Config &config)
{
    // 已启动的定时器不能在运行期间换配置，重新配置前需要明确释放其资源。
    if (resources_claimed_)
    {
        return false;
    }
    if (config.in1_channel == config.in2_channel ||
        !IsActiveHighPwm1(config.pwm_timer, config.in1_channel) ||
        !IsActiveHighPwm1(config.pwm_timer, config.in2_channel) ||
        __HAL_TIM_GET_AUTORELOAD(config.pwm_timer) > 65534U || config.rpm_period_ms == 0 ||
        config.rpm_period_ms > 1000 || config.reversal_deadtime_ms == 0 ||
        config.reversal_deadtime_ms > 1000 || !isfinite(config.counts_per_output_rev) ||
        config.counts_per_output_rev <= 0 || !isfinite(config.deadzone_duty) ||
        config.deadzone_duty < 0 || !isfinite(config.maximum_duty) ||
        config.maximum_duty <= config.deadzone_duty || config.maximum_duty > 1 ||
        (config.encoder_timer &&
         (!config.encoder_timer->Instance ||
          !IS_TIM_ENCODER_INTERFACE_INSTANCE(config.encoder_timer->Instance) ||
          __HAL_TIM_GET_AUTORELOAD(config.encoder_timer) == 0 ||
          __HAL_TIM_GET_AUTORELOAD(config.encoder_timer) > 65535U ||
          config.encoder_timer->Instance == config.pwm_timer->Instance)))
    {
        return false;
    }
    config_ = config;
    if (!ClaimResources())
    {
        return false;
    }
    if (BspTIMPWM_Init(&in1_, config.pwm_timer, config.in1_channel) != HAL_OK ||
        BspTIMPWM_Init(&in2_, config.pwm_timer, config.in2_channel) != HAL_OK || !WritePair(0, 0))
    {
        DeInit();
        return false;
    }
    // 在启动任何通道之前，两个输入都已经为零。
    if (BspTIMPWM_Start(&in1_) != HAL_OK || BspTIMPWM_Start(&in2_) != HAL_OK)
    {
        DeInit();
        return false;
    }
    if (config.encoder_timer)
    {
        encoder_started_ = true; // HAL 部分启动后失败也要执行清理。
        if (BspEncoder_Init(&encoder_, config.encoder_timer) != HAL_OK)
        {
            DeInit();
            return false;
        }
    }
    initialized_ = true;
    last_direction_ = 0;
    applied_duty_ = 0;
    rpm_tick_ = coast_tick_ = HAL_GetTick();
    rpm_count_ = 0;
    return true;
}

bool MotorPwmDriver::WritePair(float first, float second)
{
    return BspTIMPWM_WritePair(&in1_, first, &in2_, second) == HAL_OK;
}

void MotorPwmDriver::Fail()
{
    initialized_ = false;
    WritePair(0, 0);
    applied_duty_ = 0;
    (void)BspTIMPWM_Stop(&in1_);
    (void)BspTIMPWM_Stop(&in2_);
    if (encoder_started_)
    {
        (void)HAL_TIM_Encoder_Stop(config_.encoder_timer, TIM_CHANNEL_ALL);
        encoder_started_ = false;
    }
}

void MotorPwmDriver::Stop()
{
    if (!resources_claimed_ || applied_duty_ == 0)
    {
        return;
    }
    // repeated Stop 不能重置滑行起点，否则下一次反向会一直等不到间隔结束。
    if (applied_duty_ != 0)
    {
        coast_tick_ = HAL_GetTick();
    }
    if (!WritePair(0, 0))
    {
        Fail();
        return;
    }
    applied_duty_ = 0;
}

bool MotorPwmDriver::SetDuty(float duty)
{
    if (!initialized_ || !isfinite(duty))
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
    if (duty != 0)
    {
        float magnitude = fabsf(duty) + config_.deadzone_duty;
        if (magnitude > config_.maximum_duty)
        {
            magnitude = config_.maximum_duty;
        }
        duty = duty > 0 ? magnitude : -magnitude;
    }
    if (config_.output_reverse)
    {
        duty = -duty;
    }
    if (duty == 0)
    {
        Stop();
        return true;
    }

    const int8_t direction = duty > 0 ? 1 : -1;
    if (last_direction_ != 0 && direction != last_direction_)
    {
        if (applied_duty_ != 0)
        {
            Stop();
            if (!initialized_)
            {
                return false;
            }
        }
        if ((uint32_t)(HAL_GetTick() - coast_tick_) < config_.reversal_deadtime_ms)
        {
            return true;
        }
    }
    const float logical_duty = config_.output_reverse ? -duty : duty;
    if (applied_duty_ == logical_duty)
    {
        return true; // 保持同一输出，不反复触发共享 PWM 定时器的更新事件。
    }
    if (!WritePair(duty > 0 ? duty : 0, duty < 0 ? -duty : 0))
    {
        Fail();
        return false;
    }
    applied_duty_ = logical_duty;
    last_direction_ = direction;
    return true;
}

bool MotorPwmDriver::ReadRpm(float *rpm)
{
    if (!initialized_ || !encoder_started_ || !rpm)
    {
        return false;
    }
    int32_t delta = 0;
    if (BspEncoder_SampleChecked(&encoder_, &delta) != HAL_OK)
    {
        Fail();
        return false;
    }
    const uint32_t now = HAL_GetTick();
    const uint32_t elapsed = now - rpm_tick_;
    if (elapsed < config_.rpm_period_ms)
    {
        return false;
    }
    // 过长的轮询间隔无法保证没有丢失回绕，不能把它当成新鲜速度交付。
    if (elapsed > 1000U)
    {
        Fail();
        return false;
    }
    const int64_t counts = encoder_.total - rpm_count_;
    *rpm = (float)counts * 60000.0f / config_.counts_per_output_rev / (float)elapsed;
    if (config_.encoder_reverse)
    {
        *rpm = -*rpm;
    }
    rpm_count_ = encoder_.total;
    rpm_tick_ = now;
    return isfinite(*rpm);
}

DcMotor::Driver MotorPwmDriver::GetDriver()
{
    if (!initialized_)
    {
        return {nullptr, nullptr, nullptr, nullptr};
    }
    return {this, SetDutyCallback, StopCallback, encoder_started_ ? ReadRpmCallback : nullptr};
}

bool MotorPwmDriver::SetDutyCallback(void *context, float duty)
{
    return context && static_cast<MotorPwmDriver *>(context)->SetDuty(duty);
}

void MotorPwmDriver::StopCallback(void *context)
{
    if (context)
    {
        static_cast<MotorPwmDriver *>(context)->Stop();
    }
}

bool MotorPwmDriver::ReadRpmCallback(void *context, float *rpm)
{
    return context && static_cast<MotorPwmDriver *>(context)->ReadRpm(rpm);
}

bool MotorPwmDriver::IsReady() const
{
    return initialized_;
}

float MotorPwmDriver::GetAppliedDuty() const
{
    return applied_duty_;
}

int64_t MotorPwmDriver::GetEncoderCount() const
{
    return config_.encoder_reverse ? -encoder_.total : encoder_.total;
}
