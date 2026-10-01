#pragma once
#include "stm32f1xx_hal.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* V1_main UART registration API, without its hard-coded gray sensor protocol.
     * ISR callbacks must copy data before returning; DMA TX buffers must remain
     * alive until completion. Registration does not block waiting for input. */
    typedef void (*BspUart_InstRxCallback)(UART_HandleTypeDef *, uint8_t *, uint16_t);

    typedef enum
    {
        BspUart_Type_Normal,
        BspUart_Type_IT,
        BspUart_Type_DMA
    } BspUart_Type;

    typedef struct
    {
        UART_HandleTypeDef *huart;
        BspUart_Type rx_type, tx_type;
        BspUart_InstRxCallback rx_callback;
        uint8_t rx_buf[128];
        uint8_t rx_byte;
        uint16_t rx_setlen, rx_len;
    } BspUart_Instance;

    void BspUart_InstRegister(BspUart_Instance *, UART_HandleTypeDef *, BspUart_Type, BspUart_Type,
                              uint16_t, BspUart_InstRxCallback);
    HAL_StatusTypeDef BspUart_StartReceive(BspUart_Instance *);
    HAL_StatusTypeDef BspUart_Transmit(BspUart_Instance, uint8_t *, uint16_t);
    /* Fixed-length blocking read is opt-in; never called during registration. */
    HAL_StatusTypeDef BspUart_Receive(BspUart_Instance *, uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
