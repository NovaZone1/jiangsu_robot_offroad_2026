#include "gray_yahboom_8lp.hpp"

static_assert(FRAME_GRAY_MAX_CHANNELS >= GrayYahboom8Lp::ChannelCount,
              "The existing gray sample must accommodate eight channels");

bool GrayYahboom8Lp::Init(const Channel (&channels)[ChannelCount], const Config &config)
{
    if (init_attempted_ || config.sample_period_ms == 0 || config.sample_period_ms > 50 ||
        config.debounce_samples == 0 || config.debounce_samples > 8)
    {
        return false;
    }
    // Validate the entire mapping before any registration has a side effect.
    for (uint8_t i = 0; i < ChannelCount; ++i)
    {
        const Channel &channel = channels[i];
        if (!channel.port || !channel.pin || (channel.pin & (channel.pin - 1U)) != 0 ||
            (channel.black_level != GPIO_PIN_RESET && channel.black_level != GPIO_PIN_SET))
        {
            return false;
        }
        for (uint8_t j = 0; j < i; ++j)
        {
            if (channel.port == channels[j].port && channel.pin == channels[j].pin)
            {
                return false;
            }
        }
    }

    // BSP has no unregister/transaction API. A partial failure is terminal for this
    // object; preserve its lifetime and fix resource allocation before rebooting.
    init_attempted_ = true;
    for (uint8_t i = 0; i < ChannelCount; ++i)
    {
        if (!BspGpio_InstRegister(&inputs_[i], channels[i].port, channels[i].pin, nullptr))
        {
            return false;
        }
        black_levels_[i] = channels[i].black_level;
    }
    config_ = config;
    initialized_at_ms_ = HAL_GetTick();
    initialized_ = true;
    return true;
}

void GrayYahboom8Lp::ConfirmCalibration(bool calibrated)
{
    calibrated_ = calibrated;
    frame_.valid = false;
    sampled_ = false; // Deliver an updated validity decision on the next Reader call.
    settled_mask_ = 0;
    stable_mask_ = 0;
    for (uint8_t i = 0; i < ChannelCount; ++i)
    {
        consecutive_[i] = 0;
    }
}

bool GrayYahboom8Lp::IsReady() const
{
    return initialized_ && calibrated_ &&
           (warmup_complete_ || (uint32_t)(HAL_GetTick() - initialized_at_ms_) >= WarmupMs);
}

void GrayYahboom8Lp::NotifyPowerOn()
{
    initialized_at_ms_ = HAL_GetTick();
    warmup_complete_ = false;
    ConfirmCalibration(false);
}

bool GrayYahboom8Lp::Poll()
{
    const uint32_t now = HAL_GetTick();
    if (!initialized_ || (sampled_ && (uint32_t)(now - last_sample_ms_) < config_.sample_period_ms))
    {
        return false;
    }

    warmup_complete_ = warmup_complete_ || (uint32_t)(now - initialized_at_ms_) >= WarmupMs;
    Frame next = {};
    for (uint8_t i = 0; i < ChannelCount; ++i)
    {
        const GPIO_PinState level = BspGpio_GetState(&inputs_[i]);
        next.raw[i] = level == GPIO_PIN_SET ? 1U : 0U;
        next.raw_high_mask |= (uint8_t)(next.raw[i] << i);
        const uint8_t black = level == black_levels_[i] ? 1U : 0U;
        if (consecutive_[i] == 0 || candidate_[i] != black)
        {
            candidate_[i] = black;
            consecutive_[i] = 1;
        }
        else if (consecutive_[i] < config_.debounce_samples)
        {
            ++consecutive_[i];
        }
        if (consecutive_[i] >= config_.debounce_samples)
        {
            const uint8_t bit = (uint8_t)(1U << i);
            settled_mask_ |= bit;
            if (black)
            {
                stable_mask_ |= bit;
            }
            else
            {
                stable_mask_ &= (uint8_t)~bit;
            }
        }
    }

    next.black_mask = stable_mask_;
    next.timestamp_ms = HAL_GetTick();
    next.valid = IsReady() && settled_mask_ == 0xFFU;
    frame_ = next; // Publish only after all eight channels have been read.
    last_sample_ms_ = now;
    sampled_ = true;
    return true;
}

const GrayYahboom8Lp::Frame &GrayYahboom8Lp::GetFrame() const
{
    return frame_;
}

bool GrayYahboom8Lp::Read(void *context, GraySensor::Sample *sample)
{
    if (!context || !sample)
    {
        return false;
    }
    GrayYahboom8Lp &driver = *static_cast<GrayYahboom8Lp *>(context);
    if (!driver.Poll())
    {
        return false;
    }
    const Frame &frame = driver.GetFrame();
    GraySensor::Sample next = {};
    next.channels = ChannelCount;
    next.timestamp_ms = frame.timestamp_ms;
    next.valid = frame.valid;
    for (uint8_t i = 0; i < ChannelCount; ++i)
    {
        next.line[i] = (frame.black_mask & (1U << i)) ? 0.0f : 1.0f;
    }
    *sample = next;
    return true;
}
