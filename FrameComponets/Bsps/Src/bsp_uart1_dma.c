#include "bsp_uart1_dma.h"
#include "bsp_gpio.h"
#include <string.h>

// Register-level DMA owner; deliberately does not define any HAL UART callbacks.
// RX half/full IRQs and the task poll share CaptureRx under the same short critical section.
enum
{
    RxDmaSize = 256,
    RxQueueSize = 512,
    TxSlots = 4,
    TxSize = 384
};

static BspGpio_Instance tx_pin, rx_pin;
static uint8_t rx_dma[RxDmaSize], rx_queue[RxQueueSize], tx[TxSlots][TxSize];
static uint16_t tx_length[TxSlots];
static volatile uint16_t rx_position, rx_head, rx_tail;
static volatile uint8_t rx_lost, rx_fault, tx_head, tx_tail, tx_count, tx_busy, tx_sync;
static uint8_t initialized;
static uint32_t configured_baud, tx_started;
static const uint8_t newline = '\n';
static volatile BspUart1Dma_Stats stats;

static uint32_t Lock(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    return mask;
}

static void LoseRx(void)
{
    rx_head = rx_tail = 0;
    rx_lost = 1;
}

static void StartRx(void)
{
    DMA1_Channel5->CCR = 0;
    DMA1->IFCR = DMA_IFCR_CGIF5;
    rx_position = 0;
    DMA1_Channel5->CPAR = (uintptr_t)&USART1->DR;
    DMA1_Channel5->CMAR = (uintptr_t)rx_dma;
    DMA1_Channel5->CNDTR = RxDmaSize;
    DMA1_Channel5->CCR = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PL_1 | DMA_CCR_HTIE | DMA_CCR_TCIE |
                         DMA_CCR_TEIE | DMA_CCR_EN;
}

static void CaptureRx(void)
{
    if (!initialized || rx_fault)
    {
        return;
    }
    const uint16_t end = (RxDmaSize - DMA1_Channel5->CNDTR) % RxDmaSize;
    // Both boundaries pending means interrupt latency could have hidden a full lap.
    if ((DMA1->ISR & (DMA_ISR_HTIF5 | DMA_ISR_TCIF5)) == (DMA_ISR_HTIF5 | DMA_ISR_TCIF5))
    {
        DMA1->IFCR = DMA_IFCR_CGIF5;
        ++stats.rx_overflows;
        LoseRx();
        rx_position = end;
        return;
    }
    const uint16_t count = (end + RxDmaSize - rx_position) % RxDmaSize;
    const uint16_t used = (rx_head + RxQueueSize - rx_tail) % RxQueueSize;
    if (count > RxQueueSize - 1U - used)
    {
        ++stats.rx_overflows;
        LoseRx();
        rx_position = end;
        return;
    }
    for (uint16_t i = 0; i < count; ++i)
    {
        rx_queue[rx_head] = rx_dma[rx_position];
        rx_head = (rx_head + 1U) % RxQueueSize;
        rx_position = (rx_position + 1U) % RxDmaSize;
    }
    stats.rx_bytes += count;
}

static void TxFault(void)
{
    DMA1_Channel4->CCR = 0;
    DMA1->IFCR = DMA_IFCR_CGIF4;
    tx_head = tx_tail = tx_count = tx_busy = 0;
    tx_sync = 1; // Terminate any partial wire frame before sending another one.
    ++stats.tx_errors;
}

uint8_t BspUart1Dma_Init(uint32_t baud)
{
    if (initialized)
    {
        return baud == configured_baud;
    }
    const uint32_t clock = HAL_RCC_GetPCLK2Freq();
    if (baud < 9600U || baud > 115200U || clock < baud * 16U ||
        (clock + baud / 2U) / baud > 65535U || (RCC->APB2ENR & RCC_APB2ENR_USART1EN) != 0U)
    {
        return 0;
    }
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    if ((AFIO->MAPR & AFIO_MAPR_USART1_REMAP) || DMA1_Channel4->CCR || DMA1_Channel5->CCR ||
        !BspGpio_InstRegister(&tx_pin, GPIOA, GPIO_PIN_9, 0) ||
        !BspGpio_InstRegister(&rx_pin, GPIOA, GPIO_PIN_10, 0))
    {
        return 0;
    }
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
    __HAL_RCC_USART1_CLK_ENABLE();
    USART1->CR1 = 0;
    USART1->CR2 = 0;
    USART1->BRR = (clock + baud / 2U) / baud;
    USART1->CR3 = USART_CR3_DMAR | USART_CR3_DMAT | USART_CR3_EIE;
    StartRx();
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    configured_baud = baud;
    initialized = 1;
    const IRQn_Type irqs[] = {USART1_IRQn, DMA1_Channel4_IRQn, DMA1_Channel5_IRQn};
    for (uint8_t i = 0; i < 3; ++i)
    {
        HAL_NVIC_ClearPendingIRQ(irqs[i]);
        HAL_NVIC_SetPriority(irqs[i], 7, 0);
        HAL_NVIC_EnableIRQ(irqs[i]);
    }
    return 1;
}

void BspUart1Dma_Service(void)
{
    if (!initialized)
    {
        return;
    }
    const uint32_t mask = Lock();
    if (rx_fault)
    {
        // Error IRQ stopped RX; discard the damaged stream before restarting DMA.
        LoseRx();
        (void)USART1->SR;
        (void)USART1->DR;
        StartRx();
        rx_fault = 0;
        USART1->CR3 |= USART_CR3_EIE | USART_CR3_DMAR;
    }
    CaptureRx();
    if (tx_busy && (uint32_t)(HAL_GetTick() - tx_started) > 500U)
    {
        TxFault();
    }
    if (!tx_busy && (tx_sync || tx_count))
    {
        DMA1_Channel4->CCR = 0;
        DMA1->IFCR = DMA_IFCR_CGIF4;
        DMA1_Channel4->CPAR = (uintptr_t)&USART1->DR;
        DMA1_Channel4->CMAR = tx_sync ? (uintptr_t)&newline : (uintptr_t)tx[tx_tail];
        DMA1_Channel4->CNDTR = tx_sync ? 1U : tx_length[tx_tail];
        tx_busy = tx_sync ? 2U : 1U;
        tx_started = HAL_GetTick();
        DMA1_Channel4->CCR = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_TCIE | DMA_CCR_TEIE | DMA_CCR_EN;
    }
    __set_PRIMASK(mask);
}

uint16_t BspUart1Dma_Read(uint8_t *data, uint16_t capacity, uint8_t *discontinuity)
{
    if (!data || !capacity || !discontinuity)
    {
        return 0;
    }
    const uint32_t mask = Lock();
    *discontinuity = rx_lost;
    rx_lost = 0;
    uint16_t count = 0;
    while (rx_tail != rx_head && count < capacity && count < 64U)
    {
        data[count++] = rx_queue[rx_tail];
        rx_tail = (rx_tail + 1U) % RxQueueSize;
    }
    __set_PRIMASK(mask);
    return count;
}

uint8_t BspUart1Dma_Send(const uint8_t *data, uint16_t size)
{
    const uint32_t mask = Lock();
    if (!initialized || !data || !size || size > TxSize || tx_count == TxSlots)
    {
        ++stats.tx_rejected;
        __set_PRIMASK(mask);
        return 0;
    }
    memcpy(tx[tx_head], data, size);
    tx_length[tx_head] = size;
    tx_head = (tx_head + 1U) % TxSlots;
    ++tx_count;
    __set_PRIMASK(mask);
    return 1;
}

BspUart1Dma_Stats BspUart1Dma_GetStats(void)
{
    const uint32_t mask = Lock();
    BspUart1Dma_Stats copy;
    copy.rx_bytes = stats.rx_bytes;
    copy.rx_overflows = stats.rx_overflows;
    copy.rx_errors = stats.rx_errors;
    copy.tx_errors = stats.tx_errors;
    copy.tx_rejected = stats.tx_rejected;
    __set_PRIMASK(mask);
    return copy;
}

void DMA1_Channel5_IRQHandler(void)
{
    const uint32_t mask = Lock();
    const uint32_t flags = DMA1->ISR;
    if (flags & DMA_ISR_TEIF5)
    {
        DMA1->IFCR = DMA_IFCR_CGIF5;
        DMA1_Channel5->CCR = 0;
        rx_fault = 1;
        LoseRx();
        ++stats.rx_errors;
    }
    else
    {
        CaptureRx();
        DMA1->IFCR = DMA_IFCR_CGIF5;
    }
    __set_PRIMASK(mask);
}

void DMA1_Channel4_IRQHandler(void)
{
    const uint32_t mask = Lock();
    const uint32_t flags = DMA1->ISR;
    DMA1->IFCR = DMA_IFCR_CGIF4;
    if (flags & DMA_ISR_TEIF4)
    {
        TxFault();
    }
    else if ((flags & DMA_ISR_TCIF4) && tx_busy)
    {
        DMA1_Channel4->CCR = 0;
        if (tx_busy == 2U)
        {
            tx_sync = 0;
        }
        else
        {
            tx_tail = (tx_tail + 1U) % TxSlots;
            --tx_count;
        }
        tx_busy = 0;
    }
    __set_PRIMASK(mask);
}

void USART1_IRQHandler(void)
{
    const uint32_t mask = Lock();
    const uint32_t status = USART1->SR;
    if (status & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE))
    {
        USART1->CR3 &= ~(USART_CR3_DMAR | USART_CR3_EIE);
        DMA1_Channel5->CCR = 0;
        (void)USART1->DR;
        rx_fault = 1;
        LoseRx();
        ++stats.rx_errors;
    }
    __set_PRIMASK(mask);
}
