#pragma once
#include "stm32f1xx_hal.h"
using osThreadId = void *;

enum osPriority
{
    osPriorityIdle,
    osPriorityNormal
};

inline uint32_t xTaskGetTickCount()
{
    return test_tick;
}

inline osThreadId osThreadGetId()
{
    return nullptr;
}

inline osPriority osThreadGetPriority(osThreadId)
{
    return osPriorityNormal;
}

inline void osThreadSetPriority(osThreadId, osPriority)
{
}

inline void osDelay(uint32_t ms)
{
    test_tick += ms;
}
