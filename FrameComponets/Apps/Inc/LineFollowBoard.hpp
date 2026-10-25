#pragma once

#include "LineFollowApp.hpp"

namespace LineFollowBoard
{
    // APP selection only. False restores the original Offroad entry without deleting it.
    constexpr bool Enabled = true;
    bool InitGray(GraySensor &gray, uint32_t warmup_ms = 20000);
    // Same framework task only. GPIO cannot detect KEY1 calibration completion.
    void ConfirmCalibration(bool calibrated);
    void NotifyGrayPowerOn();
} // namespace LineFollowBoard

extern LineFollowApp LineFollow;
