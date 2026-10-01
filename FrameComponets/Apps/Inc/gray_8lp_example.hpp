#pragma once

#include "gray_sensor.hpp"

namespace Gray8LpExample
{
    enum Position
    {
        Invalid,
        NoLine,
        Left,
        Center,
        Right,
        Ambiguous
    };

    struct Result
    {
        Position position;
        uint8_t black_mask; // bit 0 = vehicle left, bit 7 = vehicle right.
        float error; // [-1,1], left negative; usable only for Left/Center/Right.
    };

    // Pure black-line interpretation; does not start the robot or command motors.
    Result EvaluateBlackLine(const GraySensor::Sample &sample, uint32_t now_ms,
                             uint32_t max_age_ms = 50);

    // Explicit lossy compatibility example: OR adjacent pairs into four zones.
    // bit 0 = leftmost zone; 1 = black present. Not the old module's raw pin levels.
    uint8_t ToFourZones(uint8_t black_mask);
} // namespace Gray8LpExample
