#include "bsp_motor_board.h"

static TIM_HandleTypeDef pwm1, pwm8, encoder2, encoder3, encoder4, encoder5;
static uint8_t initialized;

static HAL_StatusTypeDef InitPwm(TIM_HandleTypeDef *timer, TIM_TypeDef *instance)
{
    const uint32_t pclk = HAL_RCC_GetPCLK2Freq();
    const uint32_t clock = pclk * ((RCC->CFGR & RCC_CFGR_PPRE2) ? 2U : 1U);
    // 20 kHz，分频为零；当前 72 MHz 对应 ARR=3599。
    if (clock % 20000U != 0 || clock / 20000U < 2U || clock / 20000U > 65535U)
    {
        return HAL_ERROR;
    }
    timer->Instance = instance;
    timer->Init.Prescaler = 0;
    timer->Init.CounterMode = TIM_COUNTERMODE_UP;
    timer->Init.Period = clock / 20000U - 1U;
    timer->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    timer->Init.RepetitionCounter = 0;
    timer->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(timer) != HAL_OK)
    {
        return HAL_ERROR;
    }

    TIM_OC_InitTypeDef output = {0};
    output.OCMode = TIM_OCMODE_PWM1;
    output.Pulse = 0;
    output.OCPolarity = TIM_OCPOLARITY_HIGH;
    output.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    output.OCFastMode = TIM_OCFAST_DISABLE;
    output.OCIdleState = TIM_OCIDLESTATE_RESET;
    output.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    for (uint32_t channel = TIM_CHANNEL_1; channel <= TIM_CHANNEL_4; channel += 4U)
    {
        if (HAL_TIM_PWM_ConfigChannel(timer, &output, channel) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }
    TIM_BreakDeadTimeConfigTypeDef protection = {0};
    protection.OffStateRunMode = TIM_OSSR_ENABLE;
    protection.OffStateIDLEMode = TIM_OSSI_ENABLE;
    protection.LockLevel = TIM_LOCKLEVEL_OFF;
    protection.DeadTime = 0; // AT8236 内部桥臂死区；软件另外提供正反转间隔。
    protection.BreakState = TIM_BREAK_DISABLE;
    protection.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
    protection.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
    return HAL_TIMEx_ConfigBreakDeadTime(timer, &protection);
}

static HAL_StatusTypeDef InitEncoder(TIM_HandleTypeDef *timer, TIM_TypeDef *instance)
{
    timer->Instance = instance;
    timer->Init.Prescaler = 0;
    timer->Init.CounterMode = TIM_COUNTERMODE_UP;
    timer->Init.Period = 65535;
    timer->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    timer->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    TIM_Encoder_InitTypeDef config = {0};
    config.EncoderMode = TIM_ENCODERMODE_TI12; // A/B 双边沿四倍频
    config.IC1Polarity = TIM_ICPOLARITY_RISING;
    config.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    config.IC1Prescaler = TIM_ICPSC_DIV1;
    config.IC1Filter = 6;
    config.IC2Polarity = TIM_ICPOLARITY_RISING;
    config.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    config.IC2Prescaler = TIM_ICPSC_DIV1;
    config.IC2Filter = 6;
    return HAL_TIM_Encoder_Init(timer, &config);
}

HAL_StatusTypeDef BspMotorBoard_Init(void)
{
    if (initialized)
    {
        return HAL_OK;
    }
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_TIM5_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_WritePin(GPIOC, gpio.Pin, GPIO_PIN_RESET);
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14;
    HAL_GPIO_WritePin(GPIOE, gpio.Pin, GPIO_PIN_RESET);
    HAL_GPIO_Init(GPIOE, &gpio);

    __HAL_AFIO_REMAP_TIM1_ENABLE(); // PE9/11/13/14
    __HAL_AFIO_REMAP_TIM2_PARTIAL_1(); // PA15/PB3
    __HAL_AFIO_REMAP_TIM3_PARTIAL(); // PB4/PB5
    __HAL_AFIO_REMAP_TIM4_ENABLE(); // PD12/PD13
    __HAL_AFIO_REMAP_SWJ_NOJTAG(); // PB3/PB4/PA15 可用，保留 SWD

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_12 | GPIO_PIN_13;
    HAL_GPIO_Init(GPIOD, &gpio);

    if (InitPwm(&pwm1, TIM1) != HAL_OK || InitPwm(&pwm8, TIM8) != HAL_OK ||
        InitEncoder(&encoder2, TIM2) != HAL_OK || InitEncoder(&encoder3, TIM3) != HAL_OK ||
        InitEncoder(&encoder4, TIM4) != HAL_OK || InitEncoder(&encoder5, TIM5) != HAL_OK)
    {
        return HAL_ERROR; // PWM 引脚仍是 GPIO 低电平
    }
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14;
    HAL_GPIO_Init(GPIOE, &gpio);
    initialized = 1;
    return HAL_OK;
}

HAL_StatusTypeDef BspMotorBoard_GetPort(uint8_t id, BspMotorBoard_Port *port)
{
    if (!initialized || !port || id < 1 || id > 4)
    {
        return HAL_ERROR;
    }
    port->pwm = id <= 2 ? &pwm8 : &pwm1;
    port->in1_channel = (id & 1U) ? TIM_CHANNEL_1 : TIM_CHANNEL_3;
    port->in2_channel = (id & 1U) ? TIM_CHANNEL_2 : TIM_CHANNEL_4;
    TIM_HandleTypeDef *encoders[] = {&encoder4, &encoder2, &encoder5, &encoder3};
    port->encoder = encoders[id - 1U];
    port->encoder_ab_swapped = id == 4; // H4A=PB5/CH2，H4B=PB4/CH1
    return HAL_OK;
}
