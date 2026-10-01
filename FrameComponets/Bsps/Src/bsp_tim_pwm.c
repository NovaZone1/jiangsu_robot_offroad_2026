#include "bsp_tim_pwm.h"
#include <math.h>
#include <string.h>

static uint8_t IsValid(const BspTIMPWM_TypeDef *inst)
{
    return inst && inst->htim && inst->htim->Instance &&
           IS_TIM_CCX_INSTANCE(inst->htim->Instance, inst->channel) &&
           __HAL_TIM_GET_AUTORELOAD(inst->htim) <= 65535U;
}

static float GetFreq(struct BspTIMPWM_t inst)
{
    if (!IsValid(&inst))
    {
        return 0;
    }
    uint32_t pclk, divider;
    if (inst.htim->Instance == TIM1 || inst.htim->Instance == TIM8)
    {
        pclk = HAL_RCC_GetPCLK2Freq();
        divider = RCC->CFGR & RCC_CFGR_PPRE2;
    }
    else
    {
        pclk = HAL_RCC_GetPCLK1Freq();
        divider = RCC->CFGR & RCC_CFGR_PPRE1;
    }
    const float clock = (float)pclk * (divider ? 2.0f : 1.0f);
    return clock / ((float)inst.htim->Instance->PSC + 1.0f) /
           ((float)__HAL_TIM_GET_AUTORELOAD(inst.htim) + 1.0f);
}

HAL_StatusTypeDef BspTIMPWM_Init(BspTIMPWM_TypeDef *inst, TIM_HandleTypeDef *htim, uint32_t channel)
{
    if (!inst)
    {
        return HAL_ERROR;
    }
    memset(inst, 0, sizeof(*inst));
    inst->htim = htim;
    inst->channel = channel;
    if (!IsValid(inst))
    {
        inst->htim = NULL;
        return HAL_ERROR;
    }
    inst->GetFreq = GetFreq;
    inst->freq = GetFreq(*inst);
    return BspTIMPWM_WriteDuty(inst, 0);
}

HAL_StatusTypeDef BspTIMPWM_WriteDuty(BspTIMPWM_TypeDef *inst, float duty)
{
    if (!IsValid(inst) || !isfinite(duty))
    {
        return HAL_ERROR;
    }
    if (duty < 0)
    {
        duty = 0;
    }
    if (duty > 1)
    {
        duty = 1;
    }
    inst->auto_reload_value = __HAL_TIM_GET_AUTORELOAD(inst->htim);
    uint32_t compare = (uint32_t)((inst->auto_reload_value + 1U) * duty);
    if (compare > 65535U)
    {
        compare = 65535U;
    }
    inst->duty = duty;
    inst->compare_value = compare;
    __HAL_TIM_SET_COMPARE(inst->htim, inst->channel, compare);
    return HAL_OK;
}

HAL_StatusTypeDef BspTIMPWM_WritePair(BspTIMPWM_TypeDef *first, float first_duty,
                                      BspTIMPWM_TypeDef *second, float second_duty)
{
    if (!IsValid(first) || !IsValid(second) || first->htim->Instance != second->htim->Instance ||
        first->channel == second->channel || !isfinite(first_duty) || !isfinite(second_duty))
    {
        return HAL_ERROR;
    }

    const uint32_t mask = __get_PRIMASK();
    __disable_irq();
    // 先清除旧指令，再写新值；UG 立即提交预装载，Stop 不等下一次 PWM 周期。
    BspTIMPWM_WriteDuty(first, 0);
    BspTIMPWM_WriteDuty(second, 0);
    BspTIMPWM_WriteDuty(first, first_duty);
    BspTIMPWM_WriteDuty(second, second_duty);
    first->htim->Instance->EGR = TIM_EGR_UG;
    __set_PRIMASK(mask);
    return HAL_OK;
}

HAL_StatusTypeDef BspTIMPWM_Start(BspTIMPWM_TypeDef *inst)
{
    if (!IsValid(inst))
    {
        return HAL_ERROR;
    }
    if (inst->enabled)
    {
        return HAL_OK;
    }
    const HAL_StatusTypeDef status = HAL_TIM_PWM_Start(inst->htim, inst->channel);
    inst->enabled = status == HAL_OK;
    return status;
}

HAL_StatusTypeDef BspTIMPWM_Stop(BspTIMPWM_TypeDef *inst)
{
    if (!IsValid(inst))
    {
        return HAL_ERROR;
    }
    if (!inst->enabled)
    {
        return HAL_OK;
    }
    const HAL_StatusTypeDef status = HAL_TIM_PWM_Stop(inst->htim, inst->channel);
    if (status == HAL_OK)
    {
        inst->enabled = 0;
    }
    return status;
}

void BspTIMPWM_InstRegist(BspTIMPWM_TypeDef *inst, TIM_HandleTypeDef *htim, uint32_t channel)
{
    (void)BspTIMPWM_Init(inst, htim, channel);
}

void BspTIMPWM_SetDuty(BspTIMPWM_TypeDef *inst, float duty)
{
    (void)BspTIMPWM_WriteDuty(inst, duty);
}

void BspTIMPWM_Enable(BspTIMPWM_TypeDef *inst)
{
    (void)BspTIMPWM_Start(inst);
}

void BspTIMPWM_Disable(BspTIMPWM_TypeDef *inst)
{
    (void)BspTIMPWM_Stop(inst);
}
