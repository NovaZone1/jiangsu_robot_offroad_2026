#include "bsp_ultrasonic_echo.h"
#include "bsp_gpio.h"
#include <assert.h>
#include <stdio.h>

GPIO_TypeDef test_gpio = {};
TIM_TypeDef test_timer = {};
RCC_TypeDef test_rcc = {};
EXTI_TypeDef test_exti = {};
DWT_TypeDef test_dwt = {};
uint32_t test_mask = 0, test_tick = 0;
static uint32_t hclk = 72000000U, pclk = 36000000U;
static unsigned other_edges = 0;

extern "C" void TIM7_IRQHandler(void);
extern "C" void EXTI15_10_IRQHandler(void);

uint32_t HAL_RCC_GetHCLKFreq(void)
{
    return hclk;
}

uint32_t HAL_RCC_GetPCLK1Freq(void)
{
    return pclk;
}

uint32_t HAL_GetTick(void)
{
    return test_tick;
}

uint32_t __get_PRIMASK(void)
{
    return test_mask;
}

void __disable_irq(void)
{
    test_mask = 1;
}

void __set_PRIMASK(uint32_t mask)
{
    test_mask = mask;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET)
    {
        port->ODR |= pin;
    }
    else
    {
        port->ODR &= ~(uint32_t)pin;
    }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    return (port->IDR & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *gpio)
{
    if (gpio->Mode == GPIO_MODE_IT_RISING_FALLING)
    {
        test_exti.IMR |= gpio->Pin;
    }
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin)
{
    port->ODR ^= pin;
}

void HAL_GPIO_LockPin(GPIO_TypeDef *, uint16_t)
{
}

void HAL_GPIO_EXTI_IRQHandler(uint16_t pin)
{
    if (test_exti.PR & pin)
    {
        test_exti.PR &= ~(uint32_t)pin;
        HAL_GPIO_EXTI_Callback(pin);
    }
}

void HAL_NVIC_ClearPendingIRQ(int)
{
}

void HAL_NVIC_SetPriority(int, uint32_t priority, uint32_t subpriority)
{
    assert(priority == 6 && subpriority == 0);
}

void HAL_NVIC_EnableIRQ(int)
{
}

static void Edge(bool high)
{
    test_gpio.IDR = high ? GPIO_PIN_12 : 0;
    test_exti.PR |= GPIO_PIN_12;
    EXTI15_10_IRQHandler();
}

static void OtherEdge(uint16_t pin)
{
    assert(pin == GPIO_PIN_10);
    ++other_edges;
}

int main()
{
    BspUltrasonicEcho_Sample sample = {};
    assert(!BspUltrasonicEcho_Start());
    assert(!BspUltrasonicEcho_Init()); // DWT not initialized.
    test_dwt.CTRL = DWT_CTRL_CYCCNTENA_Msk;
    test_rcc.CFGR = 4U << 8;
    test_rcc.APB1ENR = RCC_APB1ENR_TIM7EN;
    assert(!BspUltrasonicEcho_Init()); // Do not steal an already claimed timer.
    test_rcc.APB1ENR = 0;
    test_exti.IMR = GPIO_PIN_12;
    assert(!BspUltrasonicEcho_Init());
    test_exti.IMR = 0;
    assert(BspUltrasonicEcho_Init());
    assert(BspUltrasonicEcho_Init());
    assert(test_timer.PSC == 71 && test_timer.ARR == 19);
    assert((test_gpio.ODR & GPIO_PIN_11) == 0);
    assert(!BspUltrasonicEcho_Poll(nullptr));

    // The hardware interrupt ends TRIG even when the task does not run.
    test_mask = 1;
    assert(BspUltrasonicEcho_Start() && test_mask == 1);
    assert(test_gpio.ODR & GPIO_PIN_11);
    assert(!BspUltrasonicEcho_Start());
    test_timer.CR1 &= ~TIM_CR1_CEN; // Hardware one-pulse completion.
    test_timer.SR |= TIM_SR_UIF;
    TIM7_IRQHandler();
    assert(!(test_gpio.ODR & GPIO_PIN_11) && test_timer.DIER == 0);
    Edge(false); // Unpaired falling edge is ignored.
    assert(!BspUltrasonicEcho_Poll(&sample));
    test_dwt.CYCCNT += 7200;
    Edge(true);
    test_dwt.CYCCNT += 5850U * 72U;
    test_tick = 6;
    Edge(false);
    assert(BspUltrasonicEcho_Poll(&sample));
    assert(sample.valid && sample.pulse_us == 5850 && sample.timestamp_ms == 6);
    assert(test_mask == 1 && !BspUltrasonicEcho_Poll(&sample));
    BspUltrasonicEcho_Cancel();
    test_mask = 0;

    // Both edge subtraction and measurement deadline survive DWT rollover.
    test_dwt.CYCCNT = 0xFFFFF000U;
    assert(BspUltrasonicEcho_Start());
    Edge(true);
    test_dwt.CYCCNT += 585U * 72U;
    Edge(false);
    assert(BspUltrasonicEcho_Poll(&sample));
    assert(sample.valid && sample.pulse_us == 585);
    BspUltrasonicEcho_Cancel();

    assert(BspUltrasonicEcho_Start());
    Edge(true);
    test_dwt.CYCCNT += 35000U * 72U;
    Edge(false);
    assert(BspUltrasonicEcho_Poll(&sample) && !sample.valid);
    BspUltrasonicEcho_Cancel();
    Edge(true);
    assert(!BspUltrasonicEcho_Start()); // Stuck-high ECHO.
    Edge(false);
    assert(!BspUltrasonicEcho_Poll(&sample)); // Late echo after cancellation ignored.
    assert(BspUltrasonicEcho_Start());
    BspUltrasonicEcho_Cancel();
    assert(!(test_gpio.ODR & GPIO_PIN_11) && !(test_timer.CR1 & TIM_CR1_CEN));

    // Existing GPIO callback dispatch is retained for other shared EXTI lines.
    static BspGpio_Instance other;
    assert(BspGpio_InstRegister(&other, GPIOF, GPIO_PIN_10, OtherEdge));
    test_exti.PR |= GPIO_PIN_10;
    EXTI15_10_IRQHandler();
    assert(other_edges == 1);
    puts("PASS: ultrasonic IRQ pulse, edge capture, DWT wrap, cancel, resource guards, "
         "EXTI dispatch");
}
