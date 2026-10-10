#pragma once

#include "gray_yahboom_8lp.hpp"
#include "oled_factory.hpp"

// Host-only regression fixture; no diagnostic entry in the production firmware.
// Lifetime must cover the firmware: GrayYahboom8Lp registers member addresses.
class GrayOledTest
{
public:
    bool Init();
    void Update();

private:
    GrayYahboom8Lp sensor_;
    FactoryOled display_;
    uint32_t started_ms_ = 0;
    uint32_t last_display_ms_ = 0;
    uint8_t seen_high_ = 0;
    uint8_t seen_low_ = 0;
    bool attempted_ = false;
    bool sensor_ready_ = false;
    bool display_ready_ = false;
    bool warmed_up_ = false;
};
