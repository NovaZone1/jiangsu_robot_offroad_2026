#pragma once
/**
 * @file hyperPID.hpp
 * @author Huangney
 * @date 2026-1-25
 * @brief 更先进的PID控制器
 */

typedef struct
{
    float Kp;
    float Ki;
    float Kd;
} PIDCoeffs;

typedef struct
{
    float InteMax;
    float OutMax;
    float DeadZone;
} PIDLimits;

class HyPID
{
public:
    /// @brief PID的控制参数
    PIDCoeffs Coeffs = {};

    /// @brief PID的限制参数
    PIDLimits Limits = {};

    /* V1_main only defines parameter containers here. No calculation API
     * exists upstream; use PidGeneral from pid.hpp for working PID control. */
};
