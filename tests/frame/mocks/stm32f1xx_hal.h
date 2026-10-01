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

// 定时器模型用于运行真实 PWM/编码器 BSP 与 AT8236 适配器。
enum HAL_StatusTypeDef
{
    HAL_OK,
    HAL_ERROR,
    HAL_BUSY,
    HAL_TIMEOUT
};

struct TIM_TypeDef
{
    uint32_t PSC, ARR, CNT, CCMR1, CCMR2, CCER, EGR;
    uint32_t CCR[4];
    uint32_t started;
};

struct TIM_HandleTypeDef
{
    TIM_TypeDef *Instance;
};

struct TestRcc
{
    uint32_t CFGR;
};

extern TestRcc test_rcc;
extern TIM_TypeDef test_tim1, test_tim2, test_tim3, test_tim4, test_tim5, test_tim8;
extern int test_pwm_start_fail_channel;
extern bool test_encoder_start_failure;
#define TIM1 (&test_tim1)
#define TIM2 (&test_tim2)
#define TIM3 (&test_tim3)
#define TIM4 (&test_tim4)
#define TIM5 (&test_tim5)
#define TIM8 (&test_tim8)
#define RCC (&test_rcc)
#define RCC_CFGR_PPRE1 0x700U
#define RCC_CFGR_PPRE2 0x3800U
#define TIM_CHANNEL_1 0U
#define TIM_CHANNEL_2 4U
#define TIM_CHANNEL_3 8U
#define TIM_CHANNEL_4 12U
#define TIM_CHANNEL_ALL 0x3CU
#define TIM_CCER_CC1P 2U
#define TIM_EGR_UG 1U
#define IS_TIM_CCX_INSTANCE(timer, channel) ((timer) && (channel) <= 12U && ((channel) % 4U) == 0U)
#define IS_TIM_ENCODER_INTERFACE_INSTANCE(timer)                                                   \
    ((timer) == TIM1 || (timer) == TIM2 || (timer) == TIM3 || (timer) == TIM4 ||                   \
     (timer) == TIM5 || (timer) == TIM8)
#define __HAL_TIM_GET_AUTORELOAD(timer) ((timer)->Instance->ARR)
#define __HAL_TIM_GET_COUNTER(timer) ((timer)->Instance->CNT)
#define __HAL_TIM_SET_COMPARE(timer, channel, value)                                               \
    ((timer)->Instance->CCR[(channel) / 4U] = (value))

inline uint32_t HAL_RCC_GetPCLK1Freq()
{
    return 36000000U;
}

inline uint32_t HAL_RCC_GetPCLK2Freq()
{
    return 72000000U;
}

inline HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    if ((int)channel == test_pwm_start_fail_channel)
    {
        return HAL_ERROR;
    }
    timer->Instance->started |= 1U << (channel / 4U);
    return HAL_OK;
}

inline HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel)
{
    timer->Instance->started &= ~(1U << (channel / 4U));
    return HAL_OK;
}

inline HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *, uint32_t)
{
    return test_encoder_start_failure ? HAL_ERROR : HAL_OK;
}

inline HAL_StatusTypeDef HAL_TIM_Encoder_Stop(TIM_HandleTypeDef *, uint32_t)
{
    return HAL_OK;
}

// 诊断串口模型：读取 TXE 后本次发送占用该字节槽，测试驱动再恢复 TXE。
struct USART_TypeDef
{
    uint32_t SR, DR;
};

struct UART_InitTypeDef
{
    uint32_t BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl, OverSampling;
};

struct UART_HandleTypeDef
{
    USART_TypeDef *Instance;
    UART_InitTypeDef Init;
};

struct GPIO_InitTypeDef
{
    uint32_t Pin, Mode, Pull, Speed;
};

extern USART_TypeDef test_usart1;
extern bool test_uart_init_failure;
#define USART1 (&test_usart1)
#define GPIOA 0
#define GPIO_PIN_9 (1U << 9U)
#define GPIO_MODE_AF_PP 1U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_HIGH 3U
#define UART_WORDLENGTH_8B 0U
#define UART_STOPBITS_1 0U
#define UART_PARITY_NONE 0U
#define UART_MODE_TX 8U
#define UART_HWCONTROL_NONE 0U
#define UART_OVERSAMPLING_16 0U
#define UART_FLAG_TXE 0x80U
#define RESET 0U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USART1_CLK_ENABLE() ((void)0)

inline void HAL_GPIO_Init(int, GPIO_InitTypeDef *)
{
}

inline HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *)
{
    return test_uart_init_failure ? HAL_ERROR : HAL_OK;
}

inline uint32_t TestUartFlag(UART_HandleTypeDef *uart, uint32_t flag)
{
    const uint32_t result = uart->Instance->SR & flag;
    uart->Instance->SR &= ~flag;
    return result;
}

#define __HAL_UART_GET_FLAG(uart, flag) TestUartFlag(uart, flag)
