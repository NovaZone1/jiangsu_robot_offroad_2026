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
        uint32_t Pin, Mode, Speed, Pull;
    } GPIO_InitTypeDef;

    typedef enum
    {
        GPIO_PIN_RESET,
        GPIO_PIN_SET
    } GPIO_PinState;

    typedef int IRQn_Type;

    typedef struct
    {
        volatile uint32_t CR1, CR2, CR3, BRR, SR, DR;
    } TestUart;

    typedef struct
    {
        volatile uint32_t CCR, CNDTR;
        uintptr_t CPAR, CMAR;
    } TestDmaChannel;

    typedef struct
    {
        uint32_t ISR, IFCR;
    } TestDma;

    typedef struct
    {
        uint32_t APB2ENR;
    } TestRcc;

    typedef struct
    {
        uint32_t MAPR;
    } TestAfio;

    extern TestUart test_uart;
    extern TestDma test_dma;
    extern TestDmaChannel test_tx, test_rx;
    extern TestRcc test_rcc;
    extern TestAfio test_afio;
    extern GPIO_TypeDef test_gpio;
#define USART1 (&test_uart)
#define DMA1 (&test_dma)
#define DMA1_Channel4 (&test_tx)
#define DMA1_Channel5 (&test_rx)
#define RCC (&test_rcc)
#define AFIO (&test_afio)
#define GPIOA (&test_gpio)
#define GPIO_PIN_9 (1U << 9)
#define GPIO_PIN_10 (1U << 10)
#define GPIO_MODE_INPUT 0U
#define GPIO_MODE_AF_PP 2U
#define GPIO_SPEED_FREQ_HIGH 3U
#define GPIO_PULLUP 1U
#define DMA_CCR_EN 1U
#define DMA_CCR_TCIE 2U
#define DMA_CCR_HTIE 4U
#define DMA_CCR_TEIE 8U
#define DMA_CCR_DIR 16U
#define DMA_CCR_CIRC 32U
#define DMA_CCR_MINC 128U
#define DMA_CCR_PL_1 8192U
#define DMA_IFCR_CGIF4 (1U << 12)
#define DMA_IFCR_CGIF5 (1U << 16)
#define DMA_ISR_TCIF4 (1U << 13)
#define DMA_ISR_TEIF4 (1U << 15)
#define DMA_ISR_TCIF5 (1U << 17)
#define DMA_ISR_HTIF5 (1U << 18)
#define DMA_ISR_TEIF5 (1U << 19)
#define USART_CR1_RE 4U
#define USART_CR1_TE 8U
#define USART_CR1_UE 8192U
#define USART_CR3_EIE 1U
#define USART_CR3_DMAR 64U
#define USART_CR3_DMAT 128U
#define USART_SR_PE 1U
#define USART_SR_FE 2U
#define USART_SR_NE 4U
#define USART_SR_ORE 8U
#define RCC_APB2ENR_USART1EN (1U << 14)
#define AFIO_MAPR_USART1_REMAP 4U
#define USART1_IRQn 37
#define DMA1_Channel4_IRQn 14
#define DMA1_Channel5_IRQn 15
#define __HAL_RCC_AFIO_CLK_ENABLE() ((void)0)
#define __HAL_RCC_DMA1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USART1_CLK_ENABLE() (test_rcc.APB2ENR |= RCC_APB2ENR_USART1EN)
    uint32_t HAL_RCC_GetPCLK2Freq(void);
    uint32_t HAL_GetTick(void);
    uint32_t __get_PRIMASK(void);
    void __disable_irq(void);
    void __set_PRIMASK(uint32_t);
    void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *);
    void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
    GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t);
    void HAL_GPIO_TogglePin(GPIO_TypeDef *, uint16_t);
    void HAL_GPIO_LockPin(GPIO_TypeDef *, uint16_t);
    void HAL_NVIC_ClearPendingIRQ(IRQn_Type);
    void HAL_NVIC_SetPriority(IRQn_Type, uint32_t, uint32_t);
    void HAL_NVIC_EnableIRQ(IRQn_Type);
    void DMA1_Channel4_IRQHandler(void);
    void DMA1_Channel5_IRQHandler(void);
    void USART1_IRQHandler(void);
#ifdef __cplusplus
}
#endif
