#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct
    {
        uint32_t IDR, ODR;
    } GPIO_TypeDef;
    typedef struct
    {
        uint32_t CR1, DIER, PSC, ARR, EGR, SR, CNT;
    } TIM_TypeDef;
    typedef struct
    {
        uint32_t CFGR, APB1ENR;
    } RCC_TypeDef;
    typedef struct
    {
        uint32_t IMR, PR;
    } EXTI_TypeDef;
    typedef struct
    {
        uint32_t CTRL, CYCCNT;
    } DWT_TypeDef;
    typedef struct
    {
        uint32_t Pin, Mode, Speed, Pull;
    } GPIO_InitTypeDef;
    typedef enum
    {
        GPIO_PIN_RESET,
        GPIO_PIN_SET
    } GPIO_PinState;

    extern GPIO_TypeDef test_gpio;
    extern TIM_TypeDef test_timer;
    extern RCC_TypeDef test_rcc;
    extern EXTI_TypeDef test_exti;
    extern DWT_TypeDef test_dwt;
    extern uint32_t test_mask, test_tick;

#define GPIOF (&test_gpio)
#define TIM7 (&test_timer)
#define RCC (&test_rcc)
#define EXTI (&test_exti)
#define DWT (&test_dwt)
#define GPIO_PIN_10 (1U << 10)
#define GPIO_PIN_11 (1U << 11)
#define GPIO_PIN_12 (1U << 12)
#define GPIO_PIN_15 (1U << 15)
#define RCC_CFGR_PPRE1 (7U << 8)
#define RCC_APB1ENR_TIM7EN (1U << 5)
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define TIM_CR1_CEN 1U
#define TIM_CR1_OPM (1U << 3)
#define TIM_DIER_UIE 1U
#define TIM_SR_UIF 1U
#define TIM_EGR_UG 1U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_IT_RISING_FALLING 2U
#define GPIO_SPEED_FREQ_HIGH 3U
#define GPIO_NOPULL 0U
#define TIM7_IRQn 55
#define EXTI15_10_IRQn 40
#define __HAL_RCC_GPIOF_CLK_ENABLE() ((void)0)
#define __HAL_RCC_AFIO_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM7_CLK_ENABLE() (test_rcc.APB1ENR |= RCC_APB1ENR_TIM7EN)
#define __HAL_GPIO_EXTI_CLEAR_IT(pin) (test_exti.PR &= ~(uint32_t)(pin))

    uint32_t HAL_RCC_GetHCLKFreq(void);
    uint32_t HAL_RCC_GetPCLK1Freq(void);
    uint32_t HAL_GetTick(void);
    uint32_t __get_PRIMASK(void);
    void __disable_irq(void);
    void __set_PRIMASK(uint32_t mask);
    void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
    GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
    void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *gpio);
    void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);
    void HAL_GPIO_LockPin(GPIO_TypeDef *port, uint16_t pin);
    void HAL_GPIO_EXTI_IRQHandler(uint16_t pin);
    void HAL_GPIO_EXTI_Callback(uint16_t pin);
    void HAL_NVIC_ClearPendingIRQ(int irq);
    void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority);
    void HAL_NVIC_EnableIRQ(int irq);
#ifdef __cplusplus
}
#endif
