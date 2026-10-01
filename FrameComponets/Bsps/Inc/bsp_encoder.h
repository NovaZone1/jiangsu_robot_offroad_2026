#pragma once
#include "stm32f1xx_hal.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* New BSP support for future DC motor module. F103 timers are 16-bit.
     * Sample often enough that movement is below half of ARR+1 per sample. */
    typedef struct
    {
        TIM_HandleTypeDef *htim;
        uint32_t last;
        int64_t total;
    } BspEncoder_Instance;

    HAL_StatusTypeDef BspEncoder_Init(BspEncoder_Instance *, TIM_HandleTypeDef *);
    int32_t BspEncoder_Sample(BspEncoder_Instance *);
    HAL_StatusTypeDef BspEncoder_SampleChecked(BspEncoder_Instance *, int32_t *delta);
#ifdef __cplusplus
}
#endif
