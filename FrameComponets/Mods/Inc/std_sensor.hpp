#pragma once
#include "bsp_gpio.h"
#include "frame_config.h"

/* V1_main GpioSensor port: bounded storage, no C++ new/delete or UART protocol.
 * Objects registered with the BSP must have static/application lifetime. */
namespace StdSensor
{
    enum TriggerPolarity
    {
        ACTIVE_HIGH,
        ACTIVE_LOW
    };
} // namespace StdSensor

class GpioSensor
{
public:
    explicit GpioSensor(uint8_t count) : count_(count <= FRAME_GRAY_MAX_CHANNELS ? count : 0)
    {
    }

    bool Init(GPIO_TypeDef **ports, uint16_t *pins, StdSensor::TriggerPolarity *polarities);
    void Update();

    bool GetState(uint8_t index) const
    {
        return initialized_ && index < count_ && states_[index];
    }

    uint8_t GetSensorNums() const
    {
        return count_;
    }

private:
    uint8_t count_;
    bool initialized_ = false;
    bool states_[FRAME_GRAY_MAX_CHANNELS] = {};
    StdSensor::TriggerPolarity polarities_[FRAME_GRAY_MAX_CHANNELS] = {};
    BspGpio_Instance pins_[FRAME_GRAY_MAX_CHANNELS] = {};
};
