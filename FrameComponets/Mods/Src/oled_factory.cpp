#include "oled_factory.hpp"
#include "bsp_oled_bus.h"
#include "stm32f1xx_hal.h"
#include <string.h>

namespace
{
    // Locally defined 5x7 glyphs, row-major bits. No vendor font library dependency.
    struct Glyph
    {
        char character;
        uint8_t rows[7];
    };
    const Glyph glyphs[] = {
        {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
        {'2', {14, 17, 1, 2, 4, 8, 31}}, {'3', {30, 1, 1, 14, 1, 1, 30}},
        {'4', {2, 6, 10, 18, 31, 2, 2}}, {'5', {31, 16, 16, 30, 1, 1, 30}},
        {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
        {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
        {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
        {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
        {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
        {'G', {14, 17, 16, 23, 17, 17, 14}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
        {'I', {14, 4, 4, 4, 4, 4, 14}}, {'J', {7, 2, 2, 2, 18, 18, 12}},
        {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
        {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 25, 21, 19, 19, 17}},
        {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
        {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
        {'S', {15, 16, 16, 14, 1, 1, 30}}, {'T', {31, 4, 4, 4, 4, 4, 4}},
        {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
        {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
        {'Y', {17, 17, 10, 4, 4, 4, 4}}, {'Z', {31, 1, 2, 4, 8, 16, 31}},
        {'-', {0, 0, 0, 31, 0, 0, 0}}, {'.', {0, 0, 0, 0, 0, 6, 6}},
        {':', {0, 6, 6, 0, 6, 6, 0}}, {'?', {14, 17, 1, 2, 4, 0, 4}}};

    uint8_t Column(char character, uint8_t column)
    {
        if (character == ' ')
        {
            return 0;
        }
        if (character >= 'a' && character <= 'z')
        {
            character = (char)(character - 'a' + 'A');
        }
        const Glyph *glyph = &glyphs[sizeof(glyphs) / sizeof(glyphs[0]) - 1U];
        for (const Glyph &candidate : glyphs)
        {
            if (candidate.character == character)
            {
                glyph = &candidate;
                break;
            }
        }
        uint8_t value = 0;
        for (uint8_t row = 0; row < 7; ++row)
        {
            if (glyph->rows[row] & (1U << (4U - column)))
            {
                value |= (uint8_t)(1U << row);
            }
        }
        return value;
    }
} // namespace

bool FactoryOled::Init()
{
    if (initialized_)
    {
        return true;
    }
    initialized_ = BspOledBus_Init() != 0;
    wait_since_ = HAL_GetTick();
    return initialized_;
}

bool FactoryOled::IsBusy() const
{
    return !initialized_ || !configured_ || active_;
}

bool FactoryOled::IsHealthy() const
{
    return healthy_;
}

bool FactoryOled::SetLine(uint8_t row, const char *text)
{
    if (active_ || row >= 4 || !text)
    {
        return false;
    }
    uint8_t *line = pixels_ + row * 128U;
    memset(line, 0, 128);
    for (uint8_t index = 0; index < 21 && text[index]; ++index)
    {
        for (uint8_t column = 0; column < 5; ++column)
        {
            line[index * 6U + column] = Column(text[index], column);
        }
    }
    return true;
}

bool FactoryOled::Refresh()
{
    if (IsBusy())
    {
        return false;
    }
    active_ = true;
    step_ = 0;
    return true;
}

void FactoryOled::Fail()
{
    healthy_ = configured_ = active_ = false;
    wait_since_ = HAL_GetTick();
    wait_ms_ = 1000; // Missing display: bounded retry, no busy loop or log flood.
}

void FactoryOled::Update()
{
    if (!initialized_)
    {
        return;
    }
    BspOledBus_Status bus = BspOledBus_Poll();
    if (bus == BspOledBus_Error)
    {
        Fail();
        return;
    }
    if (bus == BspOledBus_Busy)
    {
        return;
    }
    if (!configured_)
    {
        if ((uint32_t)(HAL_GetTick() - wait_since_) < wait_ms_)
        {
            return;
        }
        // Factory 128x32 SSD1306 setup. Display stays off until all 4 pages are written.
        static const uint8_t setup[] = {0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00,
                                       0x40, 0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8,
                                       0xDA, 0x02, 0x81, 0xCF, 0xD9, 0xF1, 0xDB,
                                       0x40, 0x2E, 0xA4, 0xA6};
        if (!BspOledBus_Send(0x00, setup, sizeof(setup)))
        {
            Fail();
            return;
        }
        configured_ = active_ = true;
        step_ = 0;
        return;
    }
    if (!active_)
    {
        return;
    }
    uint8_t accepted;
    if (step_ < 8)
    {
        uint8_t page = step_ / 2U;
        if ((step_ & 1U) == 0U)
        {
            const uint8_t position[] = {(uint8_t)(0xB0U + page), 0x00, 0x10};
            accepted = BspOledBus_Send(0x00, position, sizeof(position));
        }
        else
        {
            accepted = BspOledBus_Send(0x40, pixels_ + page * 128U, 128);
        }
    }
    else if (step_ == 8)
    {
        const uint8_t on = 0xAF;
        accepted = BspOledBus_Send(0x00, &on, 1);
    }
    else
    {
        active_ = false;
        healthy_ = true;
        return;
    }
    if (!accepted)
    {
        Fail();
        return;
    }
    ++step_;
}
