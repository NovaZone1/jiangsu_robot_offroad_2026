#pragma once
#include "stm32f1xx_hal.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* Single-rank ADC path for a future analog gray module. CubeMX configures the
     * ADC channel/sample time. Scan/DMA arrays require a separate board adapter. */
    HAL_StatusTypeDef BspAdc_Calibrate(ADC_HandleTypeDef *hadc);
    HAL_StatusTypeDef BspAdc_Read(ADC_HandleTypeDef *hadc, uint16_t *value, uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
