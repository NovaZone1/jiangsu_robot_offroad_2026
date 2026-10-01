#include "gray_yahboom_8lp.hpp"
#include "gray_8lp_example.hpp"
#include <assert.h>
#include <math.h>
#include <stdio.h>

TestDwt test_dwt = {};
TestCoreDebug test_core_debug = {};
uint32_t test_tick = 0, test_primask = 0, test_gpio_reads = 0;

static GPIO_TypeDef port_a = {}, port_b = {}, port_c = {};
static GrayYahboom8Lp driver, reversed_driver, partial_driver;

static void MakeChannels(GrayYahboom8Lp::Channel (&channels)[8], GPIO_TypeDef *port,
                         bool reversed = false, GPIO_PinState black = GPIO_PIN_RESET)
{
    for (uint8_t i = 0; i < 8; ++i)
    {
        channels[i] = {port, (uint16_t)(1U << (reversed ? 7 - i : i)), black};
    }
}

static GraySensor::Sample MakeSample(uint8_t black_mask)
{
    GraySensor::Sample sample = {};
    sample.channels = 8;
    sample.valid = true;
    sample.timestamp_ms = test_tick;
    for (uint8_t i = 0; i < 8; ++i)
    {
        sample.line[i] = black_mask & (1U << i) ? 0.0f : 1.0f;
    }
    return sample;
}

static void TestDriver()
{
    GrayYahboom8Lp::Channel channels[8];
    MakeChannels(channels, &port_a);
    GrayYahboom8Lp::Config config;
    assert(!driver.Poll());
    assert(!driver.IsReady());
    assert(!GrayYahboom8Lp::Read(nullptr, nullptr));
    config.sample_period_ms = 0;
    assert(!driver.Init(channels, config));
    config.sample_period_ms = 51;
    assert(!driver.Init(channels, config));
    config.sample_period_ms = 1;
    config.debounce_samples = 0;
    assert(!driver.Init(channels, config));
    config.debounce_samples = 9;
    assert(!driver.Init(channels, config));
    config.debounce_samples = 2;
    channels[7].pin = 3;
    assert(!driver.Init(channels, config));
    channels[7] = channels[0];
    assert(!driver.Init(channels, config));
    MakeChannels(channels, &port_a);
    channels[7].port = nullptr;
    assert(!driver.Init(channels, config));
    MakeChannels(channels, &port_a);
    assert(driver.Init(channels, config));
    assert(!driver.Init(channels, config));
    GraySensor gray;
    gray.Bind(&driver, GrayYahboom8Lp::Read);
    port_a.IDR = 0xE7; // Black center pair.
    assert(!gray.Update()); // Warmup, not calibrated.
    assert(test_gpio_reads == 8);
    assert(!gray.Update());
    assert(test_gpio_reads == 8); // Same tick must not read again.
    test_tick = 19999;
    driver.ConfirmCalibration(true);
    assert(!gray.Update());
    assert(!driver.IsReady());
    test_tick = 20000;
    assert(gray.Update());
    assert(driver.IsReady());
    assert(driver.GetFrame().raw_high_mask == 0xE7);
    assert(driver.GetFrame().black_mask == 0x18);
    assert(gray.GetSample().channels == 8);
    assert(gray.GetSample().line[3] == 0.0f && gray.GetSample().line[0] == 1.0f);
    assert(gray.GetSample().line[8] == 0.0f);
    assert(gray.HasFreshSample(test_tick + 50));
    assert(!gray.HasFreshSample(test_tick + 51));
    port_a.IDR = 0xFE;
    ++test_tick;
    assert(gray.Update());
    assert(driver.GetFrame().raw_high_mask == 0xFE);
    assert(driver.GetFrame().black_mask == 0x18); // Reject one-sample change.
    port_a.IDR = 0xE7;
    ++test_tick;
    assert(gray.Update());
    port_a.IDR = 0xFE;
    ++test_tick;
    assert(gray.Update());
    ++test_tick;
    assert(gray.Update());
    assert(driver.GetFrame().black_mask == 1);
    for (uint8_t i = 0; i < 8; ++i)
    {
        assert(driver.GetFrame().raw[i] == (i == 0 ? 0 : 1));
    }
    ++test_tick;
    assert(gray.Update()); // An unchanged GPIO level is still a newly acquired snapshot.
    assert(gray.GetSample().timestamp_ms == test_tick);
    driver.ConfirmCalibration(false);
    assert(!gray.Update()); // Invalid new frame must invalidate the old GraySensor sample.
    assert(!gray.GetSample().valid);
    driver.ConfirmCalibration(true);
    assert(!gray.Update());
    ++test_tick;
    assert(gray.Update());
    driver.NotifyPowerOn();
    driver.ConfirmCalibration(true);
    assert(!gray.Update());
    test_tick += 19999;
    assert(!gray.Update());
    ++test_tick;
    assert(gray.Update());
}

static void TestMappingAndWrap()
{
    GrayYahboom8Lp::Channel channels[8];
    MakeChannels(channels, &port_b, true, GPIO_PIN_SET);
    GrayYahboom8Lp::Config config;
    config.sample_period_ms = 5;
    config.debounce_samples = 1;
    test_tick = UINT32_MAX - 10000U;
    assert(reversed_driver.Init(channels, config));
    reversed_driver.ConfirmCalibration(true);
    port_b.IDR = 0x80;
    assert(reversed_driver.Poll());
    assert(!reversed_driver.GetFrame().valid);
    test_tick += 20000U; // Warmup across uint32 wrap.
    assert(reversed_driver.Poll());
    assert(reversed_driver.GetFrame().valid);
    assert(reversed_driver.GetFrame().black_mask == 1);
    assert(reversed_driver.GetFrame().raw[0] == 1);
    test_tick = UINT32_MAX - 2U;
    assert(reversed_driver.Poll());
    test_tick += 4;
    assert(!reversed_driver.Poll());
    ++test_tick;
    assert(reversed_driver.Poll());
    assert(reversed_driver.GetFrame().valid);
}

static void TestPartialRegistration()
{
    static BspGpio_Instance occupied = {};
    assert(BspGpio_InstRegister(&occupied, &port_c, 1U << 3, nullptr));
    GrayYahboom8Lp::Channel channels[8];
    MakeChannels(channels, &port_c);
    GrayYahboom8Lp::Config config;
    assert(!partial_driver.Init(channels, config));
    assert(!partial_driver.Poll());
    assert(!partial_driver.Init(channels, config));
    assert(!partial_driver.IsReady());
}

static void TestBlackLineExample()
{
    using namespace Gray8LpExample;
    test_tick = 100;
    assert(EvaluateBlackLine(MakeSample(0), test_tick).position == NoLine);
    assert(EvaluateBlackLine(MakeSample(0xFF), test_tick).position == Ambiguous);
    assert(EvaluateBlackLine(MakeSample(0x81), test_tick).position == Ambiguous);
    assert(EvaluateBlackLine(MakeSample(0x18), test_tick).position == Center);
    assert(EvaluateBlackLine(MakeSample(0x08), test_tick).position == Center);
    const Result left = EvaluateBlackLine(MakeSample(1), test_tick);
    const Result right = EvaluateBlackLine(MakeSample(0x80), test_tick);
    assert(left.position == Left && left.error == -1.0f);
    assert(right.position == Right && right.error == 1.0f);
    GraySensor::Sample sample = MakeSample(0x18);
    assert(EvaluateBlackLine(sample, 150).position == Center);
    assert(EvaluateBlackLine(sample, 151).position == Invalid);
    sample.valid = false;
    assert(EvaluateBlackLine(sample, test_tick).position == Invalid);
    sample = MakeSample(1);
    sample.channels = 4;
    assert(EvaluateBlackLine(sample, test_tick).position == Invalid);
    sample.channels = 8;
    sample.line[2] = NAN;
    assert(EvaluateBlackLine(sample, test_tick).position == Invalid);
    sample.line[2] = 1.1f;
    assert(EvaluateBlackLine(sample, test_tick).position == Invalid);
    sample.line[2] = -0.1f;
    assert(EvaluateBlackLine(sample, test_tick).position == Invalid);
    sample = MakeSample(1);
    sample.timestamp_ms = UINT32_MAX - 5U;
    assert(EvaluateBlackLine(sample, 4).position == Left);
    // Exhaustively verify all eight-bit patterns and the documented pair-OR adapter.
    for (unsigned mask = 0; mask <= 255; ++mask)
    {
        const Result result = EvaluateBlackLine(MakeSample((uint8_t)mask), test_tick);
        assert(result.black_mask == mask);
        assert(result.error >= -1.0f && result.error <= 1.0f);
        const uint8_t zones = ToFourZones((uint8_t)mask);
        assert(zones < 16);
        for (unsigned zone = 0; zone < 4; ++zone)
        {
            assert(((zones >> zone) & 1U) == ((mask & (3U << (2 * zone))) != 0));
        }
    }
}

int main()
{
    TestDriver();
    TestMappingAndWrap();
    TestPartialRegistration();
    TestBlackLineExample();
    puts("Gray 8-LP tests passed (GPIO, adapter, debounce, warmup, wrap, 256 masks).");
    return 0;
}
