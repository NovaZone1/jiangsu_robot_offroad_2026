#pragma once

#include "bsp_encoder.h"
#include "bsp_tim_pwm.h"
#include "dc_motor.hpp"

// AT8236：IN1=PWM/IN2=0 正转，IN1=0/IN2=PWM 反转，双低滑行。
class MotorPwmDriver
{
public:
    struct Config
    {
        TIM_HandleTypeDef *pwm_timer = nullptr;
        uint32_t in1_channel = TIM_CHANNEL_1;
        uint32_t in2_channel = TIM_CHANNEL_2;
        TIM_HandleTypeDef *encoder_timer = nullptr;
        float counts_per_output_rev = 1040; // 310：20 × 13 × 4
        uint32_t rpm_period_ms = 10;
        uint32_t reversal_deadtime_ms = 2;
        bool output_reverse = false;
        bool encoder_reverse = false;
        float deadzone_duty = 0; // 非零控制量额外加此值；0 指令仍为双低停止。
        float maximum_duty = 1; // 补偿后的实际 PWM 上限。
    };

    MotorPwmDriver() = default;
    ~MotorPwmDriver();
    MotorPwmDriver(const MotorPwmDriver &) = delete;
    MotorPwmDriver &operator=(const MotorPwmDriver &) = delete;

    bool Init(const Config &config);
    void DeInit(); // 先禁用绑定的 DcMotor，再释放硬件资源。
    DcMotor::Driver GetDriver();
    bool IsReady() const;
    float GetAppliedDuty() const;
    int64_t GetEncoderCount() const;

private:
    static bool SetDutyCallback(void *context, float duty);
    static void StopCallback(void *context);
    static bool ReadRpmCallback(void *context, float *rpm);
    bool SetDuty(float duty);
    void Stop();
    bool ReadRpm(float *rpm);
    bool WritePair(float first, float second);
    void Fail();
    bool ClaimResources();
    void ReleaseResources();

    static MotorPwmDriver *owners_[FRAME_MAX_MOTORS];

    Config config_;
    BspTIMPWM_TypeDef in1_ = {};
    BspTIMPWM_TypeDef in2_ = {};
    BspEncoder_Instance encoder_ = {};
    bool initialized_ = false;
    bool encoder_started_ = false;
    bool resources_claimed_ = false;
    int8_t last_direction_ = 0;
    float applied_duty_ = 0;
    uint32_t coast_tick_ = 0;
    uint32_t rpm_tick_ = 0;
    int64_t rpm_count_ = 0;
};
