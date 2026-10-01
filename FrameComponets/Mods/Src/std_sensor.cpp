#include "std_sensor.hpp"

bool GpioSensor::Init(GPIO_TypeDef **ports, uint16_t *pins, StdSensor::TriggerPolarity *polarities)
{
    if (!count_ || !ports || !pins || !polarities)
    {
        return false;
    }
    initialized_ = false;
    for (uint8_t i = 0; i < count_; ++i)
    {
        if (!BspGpio_InstRegister(&pins_[i], ports[i], pins[i], nullptr))
        {
            return false;
        }
        polarities_[i] = polarities[i];
    }
    initialized_ = true;
    return true;
}

void GpioSensor::Update()
{
    if (!initialized_)
    {
        return;
    }
    for (uint8_t i = 0; i < count_; ++i)
    {
        GPIO_PinState state = BspGpio_GetState(&pins_[i]);
        states_[i] =
            state == (polarities_[i] == StdSensor::ACTIVE_HIGH ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}
