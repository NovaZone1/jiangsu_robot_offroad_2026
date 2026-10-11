#pragma once
#include "bsp_uart1_dma.h"

// UART transparent Bluetooth; pairing/AT commands depend on the actual module.
// IsReady means UART initialized, not Bluetooth paired or connected.
class Bluetooth
{
public:
    bool Init(uint32_t baud = 115200);
    void Service();
    uint16_t Read(uint8_t *data, uint16_t capacity, bool &discontinuity);
    bool Send(const uint8_t *data, uint16_t size);
    bool IsReady() const;
    BspUart1Dma_Stats GetStats() const;

private:
    bool ready_ = false;
};
