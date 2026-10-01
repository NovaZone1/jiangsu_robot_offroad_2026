#pragma once

#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        TIM_HandleTypeDef *pwm;
        uint32_t in1_channel;
        uint32_t in2_channel;
        TIM_HandleTypeDef *encoder;
        uint8_t encoder_ab_swapped;
    } BspMotorBoard_Port;

    // YB-DSF01，按原理图初始化四路；不启动 PWM，也不让电机自动转动。
    HAL_StatusTypeDef BspMotorBoard_Init(void);
    HAL_StatusTypeDef BspMotorBoard_GetPort(uint8_t motor_id, BspMotorBoard_Port *port);

#ifdef __cplusplus
}
#endif
