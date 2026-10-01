#pragma once
#include "../oled_mocks/stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C"
{
#endif
    extern GPIO_TypeDef test_gpio_a, test_gpio_c;
#ifdef __cplusplus
}
#endif
#define GPIOA (&test_gpio_a)
#define GPIOC (&test_gpio_c)
#define GPIO_PIN_0 (1U << 0)
#define GPIO_PIN_1 (1U << 1)
#define GPIO_PIN_2 (1U << 2)
#define GPIO_PIN_3 (1U << 3)
#define GPIO_PIN_4 (1U << 4)
#define GPIO_PIN_5 (1U << 5)
#define GPIO_MODE_INPUT 0U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
