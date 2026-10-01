#pragma once
#include "stm32f1xx_hal.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* Adapted from V1_main Libs/bsp_dwt. Pass current HCLK in MHz, not 168. */
    void BspDwt_Init(uint32_t cpu_freq_mhz);
    void BspDwt_CntUpdate(void);
    void BspDwt_SysTimeUpdate(void);
    float BspDwt_GetDeltaTime(uint32_t *cnt_last);
    double BspDwt_GetDeltaTime64(uint32_t *cnt_last);
    float BspDwt_GetTimeline_Sec(void);
    float BspDwt_GetTimeline_MSec(void);
    uint64_t BspDwt_GetTimeline_USec(void);
    void BspDwt_Delay(float seconds);
    void BspDwt_DelayUs(uint32_t us);
#ifdef __cplusplus
}
#endif
