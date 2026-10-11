#include "bluetooth.hpp"

bool Bluetooth::Init(uint32_t baud)
{
    if (ready_)
    {
        return BspUart1Dma_Init(baud) != 0;
    }
    ready_ = BspUart1Dma_Init(baud) != 0;
    return ready_;
}

void Bluetooth::Service()
{
    if (ready_)
    {
        BspUart1Dma_Service();
    }
}

uint16_t Bluetooth::Read(uint8_t *data, uint16_t capacity, bool &discontinuity)
{
    uint8_t lost = 0;
    const uint16_t size = ready_ ? BspUart1Dma_Read(data, capacity, &lost) : 0;
    discontinuity = lost != 0;
    return size;
}

bool Bluetooth::Send(const uint8_t *data, uint16_t size)
{
    return ready_ && BspUart1Dma_Send(data, size) != 0;
}

bool Bluetooth::IsReady() const
{
    return ready_;
}

BspUart1Dma_Stats Bluetooth::GetStats() const
{
    return BspUart1Dma_GetStats();
}
