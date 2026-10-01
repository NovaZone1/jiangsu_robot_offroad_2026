#pragma once

#include "bsp_gpio.h"
#include "gray_sensor.hpp"

// Yahboom 8-LP, eight independent digital outputs. Not the multiplexed 8-GS.
// Own this object for the entire firmware lifetime: the BSP keeps its addresses.
class GrayYahboom8Lp
{
public:
    enum
    {
        ChannelCount = 8,
        WarmupMs = 20000
    };

    struct Channel
    {
        GPIO_TypeDef *port;
        uint16_t pin;
        GPIO_PinState black_level; // Verify on the calibrated physical module.
    };

    struct Config
    {
        uint32_t sample_period_ms = 1;
        uint8_t debounce_samples = 2; // Consecutive host samples, not module ADC frames.
    };

    struct Frame
    {
        uint8_t raw[ChannelCount]; // Electrical 0/1, vehicle left to right.
        uint8_t raw_high_mask; // bit 0 = logical leftmost channel.
        uint8_t black_mask; // Debounced: each set bit means black; meaningful only if valid.
        uint32_t timestamp_ms; // Host GPIO acquisition time, same base as HAL_GetTick.
        bool valid; // Warmup + operator confirmation + initial debounce completed.
    };

    GrayYahboom8Lp() = default;
    GrayYahboom8Lp(const GrayYahboom8Lp &) = delete;
    GrayYahboom8Lp &operator=(const GrayYahboom8Lp &) = delete;

    // CubeMX must enable clocks and configure all eight pins as digital inputs first.
    // channels[] must be ordered VEHICLE LEFT TO RIGHT, regardless of PCB orientation.
    // Init registers GPIOs only; it does not overwrite board configuration.
    bool Init(const Channel (&channels)[ChannelCount], const Config &config);

    // Call false BEFORE manual calibration; true only after the board reports success.
    // GPIO mode cannot read the module's calibration status or detect a stuck output.
    void ConfirmCalibration(bool calibrated);
    // Call after a known module-only reset/power cycle; GPIO cannot detect it.
    void NotifyPowerOn();
    bool IsReady() const;

    // true: a new host snapshot (possibly invalid). false: not due or uninitialized.
    // Nonblocking; exactly eight GPIO reads on a due call. One task owns this object.
    bool Poll();
    const Frame &GetFrame() const;

    // Existing GraySensor::Reader contract; WHITE remains 1, BLACK remains 0.
    // Reader owns Poll(); do not also Poll() independently when using this adapter.
    static bool Read(void *context, GraySensor::Sample *sample);

private:
    BspGpio_Instance inputs_[ChannelCount] = {};
    GPIO_PinState black_levels_[ChannelCount] = {};
    Config config_;
    Frame frame_ = {};
    uint8_t candidate_[ChannelCount] = {};
    uint8_t consecutive_[ChannelCount] = {};
    uint8_t stable_mask_ = 0;
    uint8_t settled_mask_ = 0;
    uint32_t initialized_at_ms_ = 0;
    uint32_t last_sample_ms_ = 0;
    bool initialized_ = false;
    bool init_attempted_ = false;
    bool calibrated_ = false;
    bool sampled_ = false;
    bool warmup_complete_ = false;
};
