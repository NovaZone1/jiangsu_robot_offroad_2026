#include "GrayOledTest.hpp"
#include "bsp_oled_bus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

GPIO_TypeDef test_gpio = {}, test_gpio_a = {}, test_gpio_c = {};
RCC_TypeDef test_rcc = {};
OledTestI2c test_i2c = {};
OledTestAfio test_afio = {};
uint32_t test_mask = 0, test_tick = UINT32_MAX - 10000U;
static uint32_t reads = 0, input_init_count = 0;
static uint8_t screen[512] = {}, page = 0;
static GrayOledTest app;

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

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *gpio)
{
    if (gpio->Mode == GPIO_MODE_AF_OD)
    {
        assert(port == GPIOB && gpio->Pin == (GPIO_PIN_6 | GPIO_PIN_7));
    }
    else
    {
        assert(gpio->Mode == GPIO_MODE_INPUT && gpio->Pull == GPIO_NOPULL);
        assert((port == GPIOC && gpio->Pin == 0x0F) || (port == GPIOA && gpio->Pin == 0x30) ||
               (port == GPIOB && gpio->Pin == 0x03));
        ++input_init_count;
    }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    ++reads;
    return port->IDR & pin ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState)
{
    assert(false); // A read-only diagnostic must not drive sensor outputs.
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *, uint16_t)
{
    assert(false);
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

static void Transfer()
{
    if (!(test_i2c.CR1 & I2C_CR1_START))
    {
        return;
    }
    test_i2c.CR1 &= ~I2C_CR1_START;
    test_i2c.SR1 = I2C_SR1_SB;
    I2C1_EV_IRQHandler();
    assert(test_i2c.DR == 0x78);
    test_i2c.SR1 = I2C_SR1_ADDR;
    I2C1_EV_IRQHandler();
    std::vector<uint8_t> bytes;
    for (unsigned i = 0; i < 130 && (test_i2c.CR2 & I2C_CR2_ITBUFEN); ++i)
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
        page = bytes[1] - 0xB0U;
    }
    if (bytes.size() == 129 && bytes[0] == 0x40)
    {
        assert(page < 4);
        memcpy(screen + page * 128U, bytes.data() + 1, 128);
    }
}

static void Run(unsigned ms, bool attached = true)
{
    for (unsigned i = 0; i < ms; ++i)
    {
        app.Update();
        if (attached)
        {
            Transfer();
        }
        ++test_tick;
    }
}

static void SetHighMask(uint8_t mask)
{
    test_gpio_c.IDR = mask & 0x0F;
    test_gpio_a.IDR = mask & 0x30;
    test_gpio.IDR = (mask >> 6) & 3U;
}

static void CheckDigit(unsigned row, unsigned column, bool one)
{
    // Independent expected bitmap for the displayed raw digits.
    const uint8_t zero_glyph[] = {62, 81, 73, 69, 62};
    const uint8_t one_glyph[] = {0, 66, 127, 64, 0};
    assert(memcmp(screen + row * 128U + column * 6U, one ? one_glyph : zero_glyph, 5) == 0);
}

static void CheckRaw(uint8_t mask)
{
    for (unsigned i = 0; i < 8; ++i)
    {
        CheckDigit(1 + i / 4, 5 + 2 * (i % 4), mask & (1U << i));
    }
}

int main(int argc, char **)
{
    if (argc > 1)
    {
        static BspGpio_Instance occupied = {};
        assert(BspGpio_InstRegister(&occupied, GPIOC, GPIO_PIN_0, nullptr));
        assert(!app.Init());
        Run(300);
        // The numerical rows remain dashes when registration failed.
        for (unsigned row = 1; row <= 2; ++row)
        {
            for (unsigned col : {5U, 7U, 9U, 11U})
            {
                for (unsigned x = 0; x < 5; ++x)
                {
                    assert(screen[row * 128 + col * 6 + x] == 8);
                }
            }
        }
        assert(reads == 0);
        puts("PASS: GPIO registration failure displays dashes, not plausible readings");
        return 0;
    }
    app.Update();
    assert(reads == 0);
    assert(app.Init() && app.Init());
    assert(input_init_count == 3);
    SetHighMask(0xA5);
    Run(250);
    CheckRaw(0xA5);
    SetHighMask(0x5A);
    Run(250);
    CheckRaw(0x5A);
    for (unsigned i = 0; i < 8; ++i)
    {
        CheckDigit(3, 5 + i, false); // Warmup changes must not count as validation.
    }
    Run(19500); // 20 seconds reached across HAL tick rollover.
    SetHighMask(0);
    Run(250);
    SetHighMask(255);
    Run(250);
    CheckRaw(255);
    for (unsigned i = 0; i < 8; ++i)
    {
        CheckDigit(3, 5 + i, true);
    }
    for (unsigned mask = 0; mask < 256; ++mask)
    {
        SetHighMask((uint8_t)mask);
        Run(120);
        CheckRaw((uint8_t)mask);
    }
    const uint32_t before = reads;
    Run(500, false); // Missing I2C interrupts: bounded timeout, sampling still runs.
    assert(reads == before + 500 * 8);
    SetHighMask(0x81);
    Run(1500);
    CheckRaw(0x81);
    assert(test_mask == 0);
    puts("PASS: GPIO-to-OLED pixels for 256 masks, pin map, warmup/wrap, flips, "
         "display timeout/recovery, sampling continues offline");
}
