#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
    /* Factory board: PF11 TRIG, PF12 ECHO, TIM7 trigger pulse, EXTI12 echo.
     * One device, one task owner. Init after System.Init() enables DWT.
     * No HAL callback is replaced; GPIO edges use the existing BSP dispatcher. */
    typedef struct
    {
        uint32_t pulse_us;
        uint32_t timestamp_ms;
        uint8_t valid;
    } BspUltrasonicEcho_Sample;

    uint8_t BspUltrasonicEcho_Init(void);
    uint8_t BspUltrasonicEcho_Start(void);
    uint8_t BspUltrasonicEcho_Poll(BspUltrasonicEcho_Sample *sample);
    void BspUltrasonicEcho_Cancel(void);

    // Vector entries owned by this adapter; do not define them a second time.
    void TIM7_IRQHandler(void);
    void EXTI15_10_IRQHandler(void);
#ifdef __cplusplus
}
#endif
