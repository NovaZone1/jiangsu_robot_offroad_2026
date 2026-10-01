#pragma once
#include <stdint.h>
#include <stddef.h>

struct TestDwt
{
    uint32_t CYCCNT, CTRL;
};

struct TestCoreDebug
{
    uint32_t DEMCR;
};

extern TestDwt test_dwt;
extern TestCoreDebug test_core_debug;
extern uint32_t test_tick, test_primask;
#define DWT (&test_dwt)
#define CoreDebug (&test_core_debug)
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U

inline uint32_t __get_PRIMASK()
{
    return test_primask;
}

inline void __disable_irq()
{
    test_primask = 1;
}

inline void __set_PRIMASK(uint32_t mask)
{
    test_primask = mask;
}

inline void __NOP()
{
    ++test_dwt.CYCCNT;
}

inline uint32_t HAL_GetTick()
{
    return test_tick;
}

inline uint32_t HAL_RCC_GetHCLKFreq()
{
    return 72000000U;
}

inline void HAL_Delay(uint32_t ms)
{
    test_tick += ms;
}
