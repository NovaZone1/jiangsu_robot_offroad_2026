#include "bsp_adc.h"

HAL_StatusTypeDef BspAdc_Calibrate(ADC_HandleTypeDef *hadc)
{
    return hadc ? HAL_ADCEx_Calibration_Start(hadc) : HAL_ERROR;
}

HAL_StatusTypeDef BspAdc_Read(ADC_HandleTypeDef *hadc, uint16_t *value, uint32_t timeout_ms)
{
    if (!hadc || !value)
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef status = HAL_ADC_Start(hadc);
    if (status != HAL_OK)
    {
        return status;
    }
    status = HAL_ADC_PollForConversion(hadc, timeout_ms);
    if (status == HAL_OK)
    {
        *value = (uint16_t)HAL_ADC_GetValue(hadc);
    }
    HAL_StatusTypeDef stop = HAL_ADC_Stop(hadc);
    return status == HAL_OK ? stop : status;
}
