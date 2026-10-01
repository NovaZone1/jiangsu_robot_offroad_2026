#include "std_actuator.hpp"

/**
 * @brief 初始化执行器
 * @param port GPIO端口
 * @param pin GPIO引脚
 * @param trigger_mode 执行器触发模式（高电平触发或低电平触发）
 * @note
 * 该函数会注册一个GPIO实例，并根据指定的触发模式初始化执行器状态为关闭。用户需要确保在调用该函数之前已经正确配置了GPIO端口和引脚。
 */
void Actuator::Init(GPIO_TypeDef *port, uint16_t pin, Trigger_Mode triggler_mode)
{
    initialized = false;
    this->trigger_mode = triggler_mode;

    // 注册GPIO实例，暂不使用外部中断回调函数
    initialized = BspGpio_InstRegister(&gpio_inst, port, pin, nullptr) != 0;
    // 初始化执行器状态为关闭
    _SetState(OFF);
}

/**
 * @brief 打开执行器
 * @note 该函数会检查执行器是否已初始化，并调用私有函数SetState将执行器状态设置为开启。
 */
void Actuator::On()
{
    if (!initialized)
    {
        return;
    }

    _SetState(ON);
}

/**
 * @brief 关闭执行器
 * @note 该函数会检查执行器是否已初始化，并调用私有函数SetState将执行器状态设置为关闭。
 */
void Actuator::Off()
{
    if (!initialized)
    {
        return;
    }

    _SetState(OFF);
}

/**
 * @brief 设置执行器状态
 * @param state 目标状态（开启或关闭）
 * @note
 * 该函数根据执行器的触发模式（高电平触发或低电平触发）来设置GPIO引脚的状态，从而控制执行器的开关。
 */
void Actuator::_SetState(State state)
{
    if (!initialized)
    {
        return;
    }

    GPIO_PinState pin_state;
    if (this->trigger_mode == Trigger_High)
    {
        pin_state = (state == ON) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    else
    {
        pin_state = (state == ON) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }

    this->state = state;
    BspGpio_SetState(&gpio_inst, pin_state);
}
