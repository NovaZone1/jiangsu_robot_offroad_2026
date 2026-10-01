#include "bsp_tim_pwm.h"
#include <math.h>

/**
 * @brief  获取PWM频率
 * @param  pwm_inst PWM实例
 * @retval PWM频率（Hz）
 */
static float GetFreq(struct BspTIMPWM_t pwm_inst)
{
    // 检验定时器句柄有效性
    if (pwm_inst.htim == NULL || pwm_inst.htim->Instance == NULL)
    {
        return 0.0f; // 定时器句柄无效
    }

    // TIM1/TIM8 位于 APB2，其余定时器位于 APB1；总线分频时定时器时钟倍频。
    uint32_t pclk, divider;
    if (pwm_inst.htim->Instance == TIM1 || pwm_inst.htim->Instance == TIM8)
    {
        pclk = HAL_RCC_GetPCLK2Freq();
        divider = RCC->CFGR & RCC_CFGR_PPRE2;
    }
    else
    {
        pclk = HAL_RCC_GetPCLK1Freq();
        divider = RCC->CFGR & RCC_CFGR_PPRE1;
    }
    uint32_t timer_clock_freq = pclk * (divider ? 2U : 1U) / (pwm_inst.htim->Instance->PSC + 1U);
    // 计算PWM频率 = 定时器时钟频率 / (ARR + 1)
    float pwm_freq = (float)timer_clock_freq / (pwm_inst.auto_reload_value + 1);

    return pwm_freq;
}

/**
 * @brief  注册PWM实例
 * @param  pwm_inst PWM实例
 * @param  htim     定时器句柄
 * @param  channel  PWM通道
 */
void BspTIMPWM_InstRegist(BspTIMPWM_TypeDef *pwm_inst, TIM_HandleTypeDef *htim, uint32_t channel)
{
    // 检验参数有效性
    if (pwm_inst == NULL || htim == NULL || htim->Instance == NULL ||
        !IS_TIM_CCX_INSTANCE(htim->Instance, channel))
    {
        return; // 参数无效
    }

    // 配置PWM实例的相关参数
    pwm_inst->htim = htim; // 定时器句柄
    pwm_inst->channel = channel; // PWM通道
    pwm_inst->enabled = 0;

    // 获取ARR寄存器的值
    pwm_inst->auto_reload_value = __HAL_TIM_GET_AUTORELOAD(pwm_inst->htim);
    // 获取CCR寄存器的值
    pwm_inst->compare_value = __HAL_TIM_GET_COMPARE(pwm_inst->htim, pwm_inst->channel);
    // 给函数指针赋值
    pwm_inst->GetFreq = GetFreq;
    // 计算PWM频率
    pwm_inst->freq = pwm_inst->GetFreq(*pwm_inst);

    // 初始化PWM的占空比为0
    BspTIMPWM_SetDuty(pwm_inst, 0.0f);
}

/**
 * @brief  设置PWM占空比
 * @param  pwm_inst PWM实例
 * @param  duty     占空比（0.0 - 1.0）
 */
void BspTIMPWM_SetDuty(BspTIMPWM_TypeDef *pwm_inst, float duty)
{
    if (!pwm_inst || !pwm_inst->htim || !isfinite(duty))
    {
        return;
    }
    // 检查输入的占空比范围
    if (duty < 0.0f)
    {
        duty = 0.0f;
    }
    if (duty > 1.0f)
    {
        duty = 1.0f;
    }

    // 更新PWM实例的占空比
    pwm_inst->duty = duty;

    // 计算CCR的对应值
    pwm_inst->auto_reload_value = __HAL_TIM_GET_AUTORELOAD(pwm_inst->htim);
    pwm_inst->compare_value = (uint32_t)((pwm_inst->auto_reload_value + 1U) * duty);
    if (pwm_inst->compare_value > 65535U)
    {
        pwm_inst->compare_value = 65535U;
    }
    // 更新定时器的比较寄存器
    __HAL_TIM_SET_COMPARE(pwm_inst->htim, pwm_inst->channel, pwm_inst->compare_value);
}

/**
 * @brief  启用PWM输出
 * @param  pwm_inst PWM实例
 */
void BspTIMPWM_Enable(BspTIMPWM_TypeDef *pwm_inst)
{
    // 检查参数有效性
    if (pwm_inst == NULL || pwm_inst->htim == NULL)
    {
        return; // 参数无效
    }

    if (!pwm_inst->enabled)
    {
        // 启动PWM输出
        pwm_inst->enabled = HAL_TIM_PWM_Start(pwm_inst->htim, pwm_inst->channel) == HAL_OK;
    }
}

/**
 * @brief  禁用PWM输出
 * @param  pwm_inst PWM实例
 */
void BspTIMPWM_Disable(BspTIMPWM_TypeDef *pwm_inst)
{
    // 检查参数有效性
    if (pwm_inst == NULL || pwm_inst->htim == NULL)
    {
        return; // 参数无效
    }

    if (pwm_inst->enabled)
    {
        // 停止PWM输出
        if (HAL_TIM_PWM_Stop(pwm_inst->htim, pwm_inst->channel) == HAL_OK)
        {
            pwm_inst->enabled = 0;
        }
    }
}
