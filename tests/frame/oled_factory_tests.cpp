#include "bsp_oled_bus.h"
#include "oled_factory.hpp"
#include "RangeDisplayApp.hpp"
#include "ultrasonic_factory.hpp"
#include "bsp_ultrasonic_echo.h"
#include "stm32f1xx_hal.h"
#include "System.hpp"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

GPIO_TypeDef test_gpio = {};
TIM_TypeDef test_timer = {};
RCC_TypeDef test_rcc = {};
EXTI_TypeDef test_exti = {};
DWT_TypeDef test_dwt = {};
OledTestI2c test_i2c = {};
OledTestAfio test_afio = {};
OledTestSystem System;
uint32_t test_mask = 0, test_tick = 0;
static bool echo_ready = false;
static BspUltrasonicEcho_Sample echo_result = {};
static uint8_t screen[512] = {};
static uint8_t page_index = 0;
static unsigned page_writes = 0;

uint32_t HAL_RCC_GetHCLKFreq(void)
{
    return 72000000U;
}

uint32_t HAL_RCC_GetPCLK1Freq(void)
{
    return 36000000U;
}

uint32_t HAL_GetTick(void)
{
    return test_tick;
}

uint32_t __get_PRIMASK(void)
{
    return test_mask;
}

void __disable_irq(void)
{
    test_mask = 1;
}

void __set_PRIMASK(uint32_t mask)
{
    test_mask = mask;
}

void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *gpio)
{
    assert(gpio->Pin == (GPIO_PIN_6 | GPIO_PIN_7));
    assert(gpio->Mode == GPIO_MODE_AF_OD);
}

void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState)
{
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *, uint16_t)
{
    return GPIO_PIN_RESET;
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *, uint16_t)
{
}

void HAL_GPIO_LockPin(GPIO_TypeDef *, uint16_t)
{
}

void HAL_NVIC_ClearPendingIRQ(int)
{
}

void HAL_NVIC_SetPriority(int, uint32_t priority, uint32_t subpriority)
{
    assert(priority == 7 && subpriority == 0);
}

void HAL_NVIC_EnableIRQ(int)
{
}

uint8_t BspUltrasonicEcho_Init(void)
{
    return 1;
}

uint8_t BspUltrasonicEcho_Start(void)
{
    return 1;
}

uint8_t BspUltrasonicEcho_Poll(BspUltrasonicEcho_Sample *sample)
{
    if (!echo_ready)
    {
        return 0;
    }
    *sample = echo_result;
    echo_ready = false;
    return 1;
}

void BspUltrasonicEcho_Cancel(void)
{
    echo_ready = false;
}

static std::vector<uint8_t> CompleteTransfer()
{
    std::vector<uint8_t> bytes;
    if ((test_i2c.CR1 & I2C_CR1_START) == 0U)
    {
        return bytes;
    }
    test_i2c.CR1 &= ~I2C_CR1_START;
    test_i2c.SR1 = I2C_SR1_SB;
    I2C1_EV_IRQHandler();
    assert(test_i2c.DR == 0x78);
    test_i2c.SR1 = I2C_SR1_ADDR;
    I2C1_EV_IRQHandler();
    for (unsigned count = 0; count < 130 && (test_i2c.CR2 & I2C_CR2_ITBUFEN); ++count)
    {
        test_i2c.SR1 = I2C_SR1_TXE;
        I2C1_EV_IRQHandler();
        bytes.push_back((uint8_t)test_i2c.DR);
    }
    assert(!(test_i2c.CR2 & I2C_CR2_ITBUFEN));
    test_i2c.SR1 = I2C_SR1_BTF;
    I2C1_EV_IRQHandler();
    assert(test_i2c.CR1 & I2C_CR1_STOP);
    test_i2c.CR1 &= ~I2C_CR1_STOP;
    test_i2c.SR1 = test_i2c.SR2 = 0;
    if (bytes.size() == 4 && bytes[0] == 0 && (bytes[1] & 0xFCU) == 0xB0U)
    {
        page_index = bytes[1] - 0xB0U;
    }
    if (bytes.size() == 129 && bytes[0] == 0x40)
    {
        assert(page_index < 4);
        memcpy(screen + page_index * 128U, bytes.data() + 1, 128);
        ++page_writes;
    }
    return bytes;
}

class TestDisplayApp : public RangeDisplayApp
{
public:
    using RangeDisplayApp::Start;
    using RangeDisplayApp::Update;
};

int main()
{
    const uint8_t command = 0xAE;
    assert(!BspOledBus_Send(0, &command, 1));
    test_rcc.APB1ENR = RCC_APB1ENR_I2C1EN;
    assert(!BspOledBus_Init());
    test_rcc.APB1ENR = 0;
    test_afio.MAPR = AFIO_MAPR_I2C1_REMAP;
    assert(!BspOledBus_Init());
    test_afio.MAPR = 0;
    assert(BspOledBus_Init());
    assert(test_i2c.CCR == (I2C_CCR_FS | 30U) && test_i2c.TRISE == 11);
    assert(!BspOledBus_Send(0, nullptr, 1));
    assert(!BspOledBus_Send(0, &command, 129));
    assert(!BspOledBus_Send(1, &command, 1));
    uint8_t mutable_command = 0xAE;
    test_mask = 1;
    assert(BspOledBus_Send(0, &mutable_command, 1) && test_mask == 1);
    mutable_command = 0;
    assert(BspOledBus_Poll() == BspOledBus_Busy);
    assert(!BspOledBus_Send(0, &command, 1));
    auto bytes = CompleteTransfer();
    assert(bytes.size() == 2 && bytes[1] == 0xAE);
    assert(BspOledBus_Poll() == BspOledBus_Idle && test_mask == 1);
    test_mask = 0;

    // Missing ACK and arbitration loss recover without waiting in an ISR.
    for (uint32_t error : {I2C_SR1_AF, I2C_SR1_ARLO, I2C_SR1_BERR, I2C_SR1_OVR})
    {
        assert(BspOledBus_Send(0, &command, 1));
        test_i2c.SR1 = error;
        I2C1_ER_IRQHandler();
        assert(BspOledBus_Poll() == BspOledBus_Error);
        assert(!(test_i2c.CR2 & I2C_CR2_ITEVTEN));
    }
    test_tick = 0xFFFFFFF0U;
    assert(BspOledBus_Send(0, &command, 1));
    test_tick = 14;
    assert(BspOledBus_Poll() == BspOledBus_Error);
    test_i2c.SR2 = I2C_SR2_BUSY;
    assert(!BspOledBus_Send(0, &command, 1));
    assert(BspOledBus_Poll() == BspOledBus_Error);
    test_i2c.SR2 = 0;

    // Exactly four 128-byte pages; old text is cleared and active frames are immutable.
    FactoryOled display;
    assert(display.Init());
    assert(display.SetLine(0, "100.0 CM"));
    assert(!display.SetLine(4, "bad"));
    assert(!display.SetLine(0, nullptr));
    test_tick += 99;
    display.Update();
    assert(!(test_i2c.CR1 & I2C_CR1_START));
    ++test_tick;
    display.Update();
    bytes = CompleteTransfer();
    assert(bytes.size() == 26 && bytes[1] == 0xAE);
    assert(display.IsBusy() && !display.SetLine(0, "overwrite"));
    page_writes = 0;
    for (int i = 0; i < 10; ++i)
    {
        display.Update();
        CompleteTransfer();
    }
    assert(page_writes == 4 && !display.IsBusy() && display.IsHealthy());
    assert(display.SetLine(0, "1"));
    assert(display.Refresh());
    for (int i = 0; i < 10; ++i)
    {
        display.Update();
        CompleteTransfer();
    }
    for (unsigned i = 6; i < 128; ++i)
    {
        assert(screen[i] == 0);
    }

    // Application integration: real distance adapter -> glyphs, then timeout -> dashes.
    Ultrasonic range;
    assert(FactoryUltrasonic::Init(range));
    TestDisplayApp app;
    app.Start();
    assert(app.WatchPoint()); // Display does not block existing vehicle readiness.
    test_tick += 60;
    assert(!range.Update());
    ++test_tick;
    echo_result = {585, test_tick, 1};
    echo_ready = true;
    assert(range.Update());
    for (int i = 0; i < 70; ++i)
    {
        app.Update();
        CompleteTransfer();
        test_tick += 3;
    }
    assert(app.GetStatus() == App::Normal);
    // Glyph '1' starts the rendered "10.0 CM", rather than the waiting dashes.
    const uint8_t one[] = {0, 66, 127, 64, 0};
    assert(memcmp(screen + 128, one, sizeof(one)) == 0);
    assert(!range.Update());
    test_tick += 35;
    assert(!range.Update());
    for (int i = 0; i < 100; ++i)
    {
        app.Update();
        CompleteTransfer();
        test_tick += 3;
    }
    for (unsigned i = 0; i < 5; ++i)
    {
        assert(screen[128 + i] == 8); // '-' is row 3, not a stale numeric distance.
    }

    // Runtime disconnect degrades to Warning and retries after one second.
    test_tick += 200;
    app.Update();
    app.Update();
    test_i2c.SR1 = I2C_SR1_AF;
    I2C1_ER_IRQHandler();
    app.Update();
    assert(app.GetStatus() == App::Warning && app.WatchPoint());
    unsigned previous_pages = page_writes;
    test_tick += 999;
    app.Update();
    assert(page_writes == previous_pages && !(test_i2c.CR1 & I2C_CR1_START));
    ++test_tick;
    for (int i = 0; i < 12; ++i)
    {
        app.Update();
        CompleteTransfer();
    }
    assert(app.GetStatus() == App::Normal);
    puts("PASS: OLED I2C IRQ/errors/timeout, four pages, text clearing, distance, "
         "failure, recovery");
}
