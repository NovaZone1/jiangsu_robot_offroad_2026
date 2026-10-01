#pragma once

#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C"
{
#endif
    // 测试专用 USART1 TX/PA9，板载 CH340，115200 8N1；不接收、不占用中断。
    HAL_StatusTypeDef BspMotorConsole_Init(void);
    HAL_StatusTypeDef BspMotorConsole_WriteLine(const char *line);
    void BspMotorConsole_Pump(void); // 单任务轮询，TXE 未就绪时立即返回。
#ifdef __cplusplus
}
#endif
