#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        BspOledBus_Idle,
        BspOledBus_Busy,
        BspOledBus_Error
    } BspOledBus_Status;

    /* Exclusive factory I2C1 bus, PB6/PB7, address 0x3C. Task-owned calls.
     * Send copies up to 128 bytes; the caller need not retain its buffer. */
    uint8_t BspOledBus_Init(void);
    uint8_t BspOledBus_Send(uint8_t control, const uint8_t *data, uint16_t size);
    BspOledBus_Status BspOledBus_Poll(void);
    void I2C1_EV_IRQHandler(void);
    void I2C1_ER_IRQHandler(void);
#ifdef __cplusplus
}
#endif
