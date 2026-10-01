#include "bsp_uart.h"
#include "frame_config.h"
#include <string.h>
static BspUart_Instance *instances[FRAME_MAX_UART_INSTANCES];
static uint8_t count;

void BspUart_InstRegister(BspUart_Instance *inst, UART_HandleTypeDef *huart, BspUart_Type rx,
                          BspUart_Type tx, uint16_t len, BspUart_InstRxCallback callback)
{
    if (!inst || !huart || !len || len > sizeof(inst->rx_buf) || rx > BspUart_Type_DMA ||
        tx > BspUart_Type_DMA)
    {
        return;
    }
    for (uint8_t i = 0; i < count; ++i)
    {
        if (instances[i] == inst || instances[i]->huart == huart)
        {
            return;
        }
    }
    if (count >= FRAME_MAX_UART_INSTANCES)
    {
        return;
    }
    memset(inst, 0, sizeof(*inst));
    inst->huart = huart;
    inst->rx_type = rx;
    inst->tx_type = tx;
    inst->rx_setlen = len;
    inst->rx_callback = callback;
    instances[count++] = inst;
    if (rx != BspUart_Type_Normal)
    {
        (void)BspUart_StartReceive(inst);
    }
}

HAL_StatusTypeDef BspUart_StartReceive(BspUart_Instance *inst)
{
    if (!inst || !inst->huart)
    {
        return HAL_ERROR;
    }
    if (inst->rx_type == BspUart_Type_IT)
    {
        return HAL_UART_Receive_IT(inst->huart, inst->rx_buf, inst->rx_setlen);
    }
    if (inst->rx_type == BspUart_Type_DMA && inst->huart->hdmarx)
    {
        HAL_StatusTypeDef result =
            HAL_UARTEx_ReceiveToIdle_DMA(inst->huart, inst->rx_buf, inst->rx_setlen);
        if (result == HAL_OK)
        {
            __HAL_DMA_DISABLE_IT(inst->huart->hdmarx, DMA_IT_HT);
        }
        return result;
    }
    return HAL_ERROR;
}

HAL_StatusTypeDef BspUart_Receive(BspUart_Instance *inst, uint32_t timeout_ms)
{
    if (!inst || !inst->huart || inst->rx_type != BspUart_Type_Normal)
    {
        return HAL_ERROR;
    }
    HAL_StatusTypeDef status =
        HAL_UART_Receive(inst->huart, inst->rx_buf, inst->rx_setlen, timeout_ms);
    if (status == HAL_OK)
    {
        inst->rx_len = inst->rx_setlen;
        if (inst->rx_callback)
        {
            inst->rx_callback(inst->huart, inst->rx_buf, inst->rx_len);
        }
    }
    return status;
}

HAL_StatusTypeDef BspUart_Transmit(BspUart_Instance inst, uint8_t *data, uint16_t len)
{
    if (!inst.huart || !data || !len)
    {
        return HAL_ERROR;
    }
    switch (inst.tx_type)
    {
    case BspUart_Type_Normal:
        return HAL_UART_Transmit(inst.huart, data, len, 20);
    case BspUart_Type_IT:
        return HAL_UART_Transmit_IT(inst.huart, data, len);
    case BspUart_Type_DMA:
        return inst.huart->hdmatx ? HAL_UART_Transmit_DMA(inst.huart, data, len) : HAL_ERROR;
    default:
        return HAL_ERROR;
    }
}

static void Dispatch(UART_HandleTypeDef *huart, uint16_t size)
{
    for (uint8_t i = 0; i < count; ++i)
    {
        BspUart_Instance *inst = instances[i];
        if (inst->huart != huart)
        {
            continue;
        }
        if (size > inst->rx_setlen)
        {
            size = inst->rx_setlen;
        }
        inst->rx_len = size;
        if (inst->rx_callback)
        {
            inst->rx_callback(huart, inst->rx_buf, size);
        }
        (void)BspUart_StartReceive(inst);
        return;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    for (uint8_t i = 0; i < count; ++i)
    {
        if (instances[i]->huart == huart && instances[i]->rx_type == BspUart_Type_IT)
        {
            Dispatch(huart, instances[i]->rx_setlen);
            return;
        }
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    /* Normal-mode RX DMA only. Circular DMA needs a producer index adapter. */
    if (HAL_UARTEx_GetRxEventType(huart) != HAL_UART_RXEVENT_HT)
    {
        Dispatch(huart, size);
    }
}
