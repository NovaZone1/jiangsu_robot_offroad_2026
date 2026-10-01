#include "bsp_encoder.h"

HAL_StatusTypeDef BspEncoder_Init(BspEncoder_Instance *inst, TIM_HandleTypeDef *htim)
{
    if (!inst || !htim || !htim->Instance || !IS_TIM_ENCODER_INTERFACE_INSTANCE(htim->Instance))
    {
        return HAL_ERROR;
    }
    inst->htim = htim;
    inst->last = __HAL_TIM_GET_COUNTER(htim);
    inst->total = 0;
    return HAL_TIM_Encoder_Start(htim, TIM_CHANNEL_ALL);
}

int32_t BspEncoder_Sample(BspEncoder_Instance *inst)
{
    if (!inst || !inst->htim)
    {
        return 0;
    }
    uint32_t now = __HAL_TIM_GET_COUNTER(inst->htim);
    int32_t modulo = (int32_t)(__HAL_TIM_GET_AUTORELOAD(inst->htim) + 1U);
    int32_t delta = (int32_t)now - (int32_t)inst->last;
    if (delta > modulo / 2)
    {
        delta -= modulo;
    }
    else if (delta < -modulo / 2)
    {
        delta += modulo;
    }
    inst->last = now;
    inst->total += delta;
    return delta;
}
