#pragma once
#include "../mocks/stm32f1xx_hal.h"

struct GPIO_TypeDef
{
    uint16_t IDR, ODR;
};

enum GPIO_PinState
{
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET = 1
};

extern uint32_t test_gpio_reads;

inline GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    ++test_gpio_reads;
    return port->IDR & pin ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

inline void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET)
    {
        port->ODR |= pin;
    }
    else
    {
        port->ODR &= (uint16_t)~pin;
    }
}

inline void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin)
{
    port->ODR ^= pin;
}

inline void HAL_GPIO_LockPin(GPIO_TypeDef *, uint16_t)
{
}
