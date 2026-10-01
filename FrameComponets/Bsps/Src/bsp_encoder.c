#include "bsp_encoder.h"
#include <string.h>

HAL_StatusTypeDef BspEncoder_Init(BspEncoder_Instance *inst, TIM_HandleTypeDef *htim)
{
    if (!inst)
    {
        return HAL_ERROR;
    }
    memset(inst, 0, sizeof(*inst));
    if (!htim || !htim->Instance || !IS_TIM_ENCODER_INTERFACE_INSTANCE(htim->Instance) ||
        __HAL_TIM_GET_AUTORELOAD(htim) == 0 || __HAL_TIM_GET_AUTORELOAD(htim) > 65535U)
    {
        return HAL_ERROR;
    }
    const HAL_StatusTypeDef status = HAL_TIM_Encoder_Start(htim, TIM_CHANNEL_ALL);
    if (status != HAL_OK)
    {
        return status;
    }
    inst->htim = htim;
    inst->last = __HAL_TIM_GET_COUNTER(htim);
    return HAL_OK;
}

HAL_StatusTypeDef BspEncoder_SampleChecked(BspEncoder_Instance *inst, int32_t *result)
{
    if (!inst || !inst->htim || !inst->htim->Instance || !result)
    {
        return HAL_ERROR;
    }
    const uint32_t arr = __HAL_TIM_GET_AUTORELOAD(inst->htim);
    const uint32_t now = __HAL_TIM_GET_COUNTER(inst->htim);
    if (arr == 0 || arr > 65535U || now > arr || inst->last > arr)
    {
        return HAL_ERROR;
    }
    const int32_t modulo = (int32_t)(arr + 1U);
    int32_t delta = (int32_t)now - (int32_t)inst->last;
    inst->last = now;
    // 恰好半周期时方向存在歧义，拒绝把错误方向交给速度环。
    if ((modulo & 1) == 0 && (delta == modulo / 2 || delta == -modulo / 2))
    {
        return HAL_ERROR;
    }
    if (delta > modulo / 2)
    {
        delta -= modulo;
    }
    else if (delta < -modulo / 2)
    {
        delta += modulo;
    }
    inst->total += delta;
    *result = delta;
    return HAL_OK;
}

int32_t BspEncoder_Sample(BspEncoder_Instance *inst)
{
    int32_t delta = 0;
    (void)BspEncoder_SampleChecked(inst, &delta);
    return delta;
}
