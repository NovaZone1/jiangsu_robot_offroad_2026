#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "stm32f1xx_hal.h"
#include "gpio.h"

    // 定义外部中断回调函数类型
    typedef void (*BspGpio_ExtiCallback)(uint16_t pin);

    // 定义BSP_GPIO实例
    typedef struct
    {
        GPIO_TypeDef *port; // GPIO端口
        uint16_t pin; // GPIO引脚
        BspGpio_ExtiCallback exti_callback; // 外部中断回调函数
    } BspGpio_Instance;

    /// @brief 注册BSP_GPIO实例
    uint8_t BspGpio_InstRegister(BspGpio_Instance *inst, GPIO_TypeDef *port, uint16_t pin,
                                 BspGpio_ExtiCallback exti_callback);
    void BspGpio_ExtiCallbackManager(uint16_t pin);

    /// @brief 获取GPIO引脚的状态
    GPIO_PinState BspGpio_GetState(BspGpio_Instance *inst);

    /// @brief 设置GPIO引脚的状态
    void BspGpio_SetState(BspGpio_Instance *inst, GPIO_PinState state);

    /// @brief 切换GPIO引脚的状态
    void BspGpio_ToggleState(BspGpio_Instance *inst);

    /// @brief 锁定GPIO引脚配置
    void BspGpio_Lock(BspGpio_Instance *inst);

#ifdef __cplusplus
}
#endif
