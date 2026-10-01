#pragma once
#include <stdint.h>
// MEASURED profiles: raw PWM, deadzone_duty MUST be 0, feedback tau=0.03 s.
// Configure while disabled. Select the profile for the requested direction.
// Only validated at the speeds/load recorded in adjacent result.json.
struct MotorPiProfile { uint8_t port; int8_t direction; float kp, ki, kd, limit; };
static const MotorPiProfile motor_pi_profiles[] = {
    {1, 1, 0.001258304f, 0.019440701f, 0.0f, 0.690000f},
    {1, -1, 0.001254813f, 0.019377132f, 0.0f, 0.690000f},
    {2, 1, 0.001273799f, 0.019587539f, 0.0f, 0.690000f},
    {2, -1, 0.001455289f, 0.024360653f, 0.0f, 0.690000f},
    {3, 1, 0.001403675f, 0.024272447f, 0.0f, 0.690000f},
    {3, -1, 0.001455132f, 0.024353350f, 0.0f, 0.690000f},
    {4, 1, 0.001259563f, 0.019614898f, 0.0f, 0.690000f},
    {4, -1, 0.001461216f, 0.024511078f, 0.0f, 0.690000f},
};
