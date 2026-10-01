#include "bsp_ultrasonic_echo.h"
#include "bsp_gpio.h"

/* TIM6 remains the HAL time base. TIM7 generates only one 20 us pulse.
 * DWT timestamps both echo edges; no polling loop or delay is used. */
static BspGpio_Instance trigger_pin;
static BspGpio_Instance echo_pin;
static uint32_t cycles_per_us;
static uint8_t initialized;
static volatile uint8_t armed, rising_seen, result_ready;
static volatile uint32_t start_cycle, rise_cycle;
static volatile BspUltrasonicEcho_Sample result;

static void EchoEdge(uint16_t pin)
{
    uint32_t cycle = DWT->CYCCNT;
    if (pin != GPIO_PIN_12 || !armed)
    {
        return;
    }
    if (HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_12) == GPIO_PIN_SET)
    {
        if (!rising_seen)
        {
            rise_cycle = cycle;
            rising_seen = 1;
        }
        return;
    }
    if (rising_seen)
    {
        uint32_t elapsed = cycle - start_cycle;
        result.pulse_us = (cycle - rise_cycle) / cycles_per_us;
        result.timestamp_ms = HAL_GetTick();
        result.valid = elapsed < cycles_per_us * 35000U;
        armed = 0;
        result_ready = 1;
    }
}

uint8_t BspUltrasonicEcho_Init(void)
{
    if (initialized)
    {
        return 1;
    }
    uint32_t hclk = HAL_RCC_GetHCLKFreq();
    uint32_t timer_hz = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U)
    {
        timer_hz *= 2U;
    }
    if (hclk < 1000000U || hclk % 1000000U != 0U || timer_hz < 1000000U ||
        timer_hz % 1000000U != 0U || timer_hz / 1000000U > 65536U ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        return 0;
    }
    /* Reject a timer already claimed by another peripheral module. */
    if ((RCC->APB1ENR & RCC_APB1ENR_TIM7EN) != 0U ||
        (EXTI->IMR & GPIO_PIN_12) != 0U)
    {
        return 0;
    }
    if (!BspGpio_InstRegister(&trigger_pin, GPIOF, GPIO_PIN_11, 0) ||
        !BspGpio_InstRegister(&echo_pin, GPIOF, GPIO_PIN_12, EchoEdge))
    {
        return 0;
    }
    cycles_per_us = hclk / 1000000U;
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_TIM7_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_11, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOF, &gpio);
    gpio.Pin = GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOF, &gpio);

    TIM7->CR1 = TIM_CR1_OPM;
    TIM7->DIER = 0;
    TIM7->PSC = timer_hz / 1000000U - 1U;
    TIM7->ARR = 19U;
    TIM7->EGR = TIM_EGR_UG;
    TIM7->SR = 0;
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_12);
    HAL_NVIC_ClearPendingIRQ(TIM7_IRQn);
    HAL_NVIC_SetPriority(TIM7_IRQn, 6, 0);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 6, 0);
    initialized = 1;
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    return 1;
}

uint8_t BspUltrasonicEcho_Start(void)
{
    if (!initialized)
    {
        return 0;
    }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (armed || HAL_GPIO_ReadPin(GPIOF, GPIO_PIN_12) == GPIO_PIN_SET)
    {
        __set_PRIMASK(mask);
        return 0;
    }
    TIM7->CR1 &= ~TIM_CR1_CEN;
    TIM7->CNT = 0;
    TIM7->SR = 0;
    HAL_NVIC_ClearPendingIRQ(TIM7_IRQn);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_12);
    rising_seen = result_ready = 0;
    start_cycle = DWT->CYCCNT;
    armed = 1;
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_11, GPIO_PIN_SET);
    TIM7->DIER = TIM_DIER_UIE;
    TIM7->CR1 |= TIM_CR1_CEN;
    __set_PRIMASK(mask);
    return 1;
}

uint8_t BspUltrasonicEcho_Poll(BspUltrasonicEcho_Sample *sample)
{
    if (!sample || !initialized)
    {
        return 0;
    }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    uint8_t ready = result_ready;
    if (ready)
    {
        sample->pulse_us = result.pulse_us;
        sample->timestamp_ms = result.timestamp_ms;
        sample->valid = result.valid;
        result_ready = 0;
    }
    __set_PRIMASK(mask);
    return ready;
}

void BspUltrasonicEcho_Cancel(void)
{
    if (!initialized)
    {
        return;
    }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    armed = rising_seen = result_ready = 0;
    TIM7->DIER = 0;
    TIM7->CR1 &= ~TIM_CR1_CEN;
    TIM7->SR = 0;
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_11, GPIO_PIN_RESET);
    __set_PRIMASK(mask);
}

void TIM7_IRQHandler(void)
{
    if ((TIM7->SR & TIM_SR_UIF) != 0U && (TIM7->DIER & TIM_DIER_UIE) != 0U)
    {
        HAL_GPIO_WritePin(GPIOF, GPIO_PIN_11, GPIO_PIN_RESET);
        TIM7->DIER = 0;
        TIM7->SR = 0;
    }
}

void EXTI15_10_IRQHandler(void)
{
    /* Preserve dispatch for every line sharing this vector. Future CubeMX
     * generation must not introduce a second definition of this handler. */
    for (uint16_t pin = GPIO_PIN_10; pin <= GPIO_PIN_15 && pin != 0U; pin <<= 1U)
    {
        HAL_GPIO_EXTI_IRQHandler(pin);
    }
}
