#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    // Dedicated USART1 owner: PA9 TX, PA10 RX, DMA1 CH4 TX / CH5 circular RX.
    // Task APIs have one caller. Service every 1 ms; never register this UART in bsp_uart.
    typedef struct
    {
        uint32_t rx_bytes, rx_overflows, rx_errors, tx_errors, tx_rejected;
    } BspUart1Dma_Stats;

    uint8_t BspUart1Dma_Init(uint32_t baud);
    void BspUart1Dma_Service(void);
    // Returns at most 64 bytes. A discontinuity invalidates any partially parsed line.
    uint16_t BspUart1Dma_Read(uint8_t *data, uint16_t capacity, uint8_t *discontinuity);
    // Copies a complete frame (1..384 bytes) into a bounded DMA queue. No blocking.
    uint8_t BspUart1Dma_Send(const uint8_t *data, uint16_t size);
    BspUart1Dma_Stats BspUart1Dma_GetStats(void);
#ifdef __cplusplus
}
#endif
