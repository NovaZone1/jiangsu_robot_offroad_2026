#include "GrayOledTest.hpp"
// Not compiled into firmware after completion of the hardware test.
#include <stdio.h>

bool GrayOledTest::Init()
{
    if (attempted_)
    {
        return sensor_ready_ && display_ready_;
    }
    attempted_ = true;
    // User-confirmed x1..x8 wiring. Display uses PCB channel order, not vehicle sides.
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {};
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &gpio);
    const GrayYahboom8Lp::Channel channels[8] = {
        {GPIOC, GPIO_PIN_0, GPIO_PIN_RESET}, {GPIOC, GPIO_PIN_1, GPIO_PIN_RESET},
        {GPIOC, GPIO_PIN_2, GPIO_PIN_RESET}, {GPIOC, GPIO_PIN_3, GPIO_PIN_RESET},
        {GPIOA, GPIO_PIN_4, GPIO_PIN_RESET}, {GPIOA, GPIO_PIN_5, GPIO_PIN_RESET},
        {GPIOB, GPIO_PIN_0, GPIO_PIN_RESET}, {GPIOB, GPIO_PIN_1, GPIO_PIN_RESET}};
    GrayYahboom8Lp::Config config;
    sensor_ready_ = sensor_.Init(channels, config);
    // Do not ConfirmCalibration(true): GPIO cannot observe KEY1 or calibration success.
    // Only raw[] is displayed. No valid GraySensor sample is supplied to vehicle control.
    display_ready_ = display_.Init();
    started_ms_ = HAL_GetTick();
    display_.SetLine(0, sensor_ready_ ? "8LP RAW WARMUP" : "GRAY INIT ERROR");
    display_.SetLine(1, "X1-4 - - - -");
    display_.SetLine(2, "X5-8 - - - -");
    display_.SetLine(3, "FLIP 00000000");
    return sensor_ready_ && display_ready_;
}

void GrayOledTest::Update()
{
    if (!attempted_)
    {
        return;
    }
    const uint32_t now = HAL_GetTick();
    if (sensor_ready_ && sensor_.Poll())
    {
        warmed_up_ = warmed_up_ || (uint32_t)(now - started_ms_) >= GrayYahboom8Lp::WarmupMs;
        if (warmed_up_)
        {
            const uint8_t high = sensor_.GetFrame().raw_high_mask;
            seen_high_ |= high;
            seen_low_ |= (uint8_t)~high;
        }
    }
    // Screen traffic and failures never prevent GPIO polling.
    display_.Update();
    if (display_.IsBusy() || (uint32_t)(now - last_display_ms_) < 100U)
    {
        return;
    }
    last_display_ms_ = now;
    if (!sensor_ready_)
    {
        display_.SetLine(0, "GRAY INIT ERROR");
    }
    else
    {
        char text[22];
        if (!warmed_up_)
        {
            const uint32_t seconds =
                (GrayYahboom8Lp::WarmupMs - (uint32_t)(now - started_ms_) + 999U) / 1000U;
            snprintf(text, sizeof(text), "8LP RAW WARM %luS", (unsigned long)seconds);
            display_.SetLine(0, text);
        }
        else
        {
            display_.SetLine(0, "RAW CHECK KEY1");
        }
        const auto &frame = sensor_.GetFrame();
        for (uint8_t row = 0; row < 2; ++row)
        {
            const uint8_t offset = row * 4U;
            snprintf(text, sizeof(text), "X%u-%u %u %u %u %u", (unsigned)(offset + 1U),
                     (unsigned)(offset + 4U), (unsigned)frame.raw[offset],
                     (unsigned)frame.raw[offset + 1U], (unsigned)frame.raw[offset + 2U],
                     (unsigned)frame.raw[offset + 3U]);
            display_.SetLine(row + 1U, text);
        }
        char flips[] = "FLIP 00000000";
        for (uint8_t i = 0; i < 8; ++i)
        {
            flips[5 + i] = (seen_high_ & seen_low_ & (1U << i)) ? '1' : '0';
        }
        display_.SetLine(3, flips);
    }
    display_.Refresh();
}
