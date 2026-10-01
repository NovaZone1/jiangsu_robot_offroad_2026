#pragma once
#include <stdint.h>

class FactoryOled
{
public:
    bool Init();
    void Update();
    // Four 8-pixel rows, up to 21 characters each; text is copied into pixels.
    // Reject edits while a frame is being transferred, avoiding torn frames.
    bool SetLine(uint8_t row, const char *text);
    bool Refresh();
    bool IsBusy() const;
    bool IsHealthy() const;

private:
    uint8_t pixels_[512] = {};
    uint32_t wait_since_ = 0;
    uint32_t wait_ms_ = 100;
    uint8_t step_ = 0;
    bool initialized_ = false;
    bool configured_ = false;
    bool active_ = false;
    bool healthy_ = false;
    void Fail();
};
