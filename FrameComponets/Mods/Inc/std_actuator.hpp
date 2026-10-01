#pragma once

#include "bsp_gpio.h"

class Actuator
{
public:
    enum State
    {
        OFF = 0,
        ON = 1,
    };

    enum Trigger_Mode
    {
        Trigger_Low = 0, // 低电平触发
        Trigger_High = 1, // 高电平触发
    };

    /// @brief 初始化执行器
    void Init(GPIO_TypeDef *port, uint16_t pin, Trigger_Mode triggler_mode);

    /// @brief 打开执行器
    void On();

    /// @brief 关闭执行器
    void Off();

private:
    BspGpio_Instance gpio_inst = {}; // 执行器所连接的 GPIO 实例
    State state = OFF;
    Trigger_Mode trigger_mode = Trigger_High;
    bool initialized = false; // 是否已初始化
    void _SetState(State state); // 设置执行器状态
};
