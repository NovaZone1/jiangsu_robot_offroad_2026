#include "bsp_oled_bus.h"
#include "bsp_gpio.h"
#include <string.h>

/* F103 I2C master transmitter. No HAL I2C module, DMA, RTOS call or busy wait.
 * EV/ER IRQs have lower priority than ultrasonic edge/trigger IRQs (6). */
enum
{
    BusIdle,
    BusSending,
    BusStopping,
    BusFault
};
static BspGpio_Instance scl_pin, sda_pin;
static uint8_t initialized;
static uint32_t peripheral_hz, started_ms;
static uint8_t tx[129];
static volatile uint16_t position, length;
static volatile uint8_t state;

static void Configure(void)
{
    I2C1->CR1 = I2C_CR1_SWRST;
    I2C1->CR1 = 0;
    I2C1->CR2 = peripheral_hz / 1000000U;
    /* Fast mode, duty 2. Round upward so SCL does not exceed 400 kHz. */
    I2C1->CCR = I2C_CCR_FS | ((peripheral_hz + 1199999U) / 1200000U);
    I2C1->TRISE = (peripheral_hz / 1000000U) * 3U / 10U + 1U;
    I2C1->OAR1 = 0x4000U;
    I2C1->CR1 = I2C_CR1_PE;
}

uint8_t BspOledBus_Init(void)
{
    if (initialized)
    {
        return 1;
    }
    peripheral_hz = HAL_RCC_GetPCLK1Freq();
    if (peripheral_hz < 4000000U || peripheral_hz > 36000000U ||
        peripheral_hz % 1000000U != 0U || (RCC->APB1ENR & RCC_APB1ENR_I2C1EN) != 0U)
    {
        return 0;
    }
    __HAL_RCC_AFIO_CLK_ENABLE();
    if ((AFIO->MAPR & AFIO_MAPR_I2C1_REMAP) != 0U)
    {
        return 0;
    }
    if (!BspGpio_InstRegister(&scl_pin, GPIOB, GPIO_PIN_6, 0) ||
        !BspGpio_InstRegister(&sda_pin, GPIOB, GPIO_PIN_7, 0))
    {
        return 0;
    }
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);
    __HAL_RCC_I2C1_CLK_ENABLE();
    Configure();
    state = BusIdle;
    initialized = 1;
    HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
    HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 7, 0);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
    return 1;
}

uint8_t BspOledBus_Send(uint8_t control, const uint8_t *data, uint16_t size)
{
    if (!initialized || !data || size == 0U || size > 128U ||
        (control != 0x00U && control != 0x40U))
    {
        return 0;
    }
    if (state != BusIdle)
    {
        return 0;
    }
    if ((I2C1->SR2 & I2C_SR2_BUSY) != 0U)
    {
        state = BusFault;
        return 0;
    }
    tx[0] = control;
    memcpy(tx + 1, data, size);
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    position = 0;
    length = size + 1U;
    started_ms = HAL_GetTick();
    state = BusSending;
    I2C1->CR2 |= I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN;
    I2C1->CR1 |= I2C_CR1_START;
    __set_PRIMASK(mask);
    return 1;
}

BspOledBus_Status BspOledBus_Poll(void)
{
    if (!initialized)
    {
        return BspOledBus_Error;
    }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (state == BusStopping && (I2C1->CR1 & I2C_CR1_STOP) == 0U &&
        (I2C1->SR2 & I2C_SR2_BUSY) == 0U)
    {
        state = BusIdle;
    }
    if (state == BusFault ||
        (state != BusIdle && (uint32_t)(HAL_GetTick() - started_ms) >= 30U))
    {
        I2C1->CR2 &= ~(I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN);
        Configure(); // Bounded reset, even if a missing device holds a bus line low.
        state = BusIdle;
        __set_PRIMASK(mask);
        return BspOledBus_Error;
    }
    BspOledBus_Status result = state == BusIdle ? BspOledBus_Idle : BspOledBus_Busy;
    __set_PRIMASK(mask);
    return result;
}

void I2C1_ER_IRQHandler(void)
{
    uint32_t errors = I2C1->SR1 & (I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR);
    if (errors != 0U)
    {
        I2C1->CR2 &= ~(I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN);
        if ((errors & I2C_SR1_ARLO) == 0U)
        {
            I2C1->CR1 |= I2C_CR1_STOP;
        }
        I2C1->SR1 &= ~errors;
        state = BusFault;
    }
}

void I2C1_EV_IRQHandler(void)
{
    if (state != BusSending)
    {
        return;
    }
    uint32_t flags = I2C1->SR1;
    if ((flags & (I2C_SR1_AF | I2C_SR1_BERR | I2C_SR1_ARLO | I2C_SR1_OVR)) != 0U)
    {
        I2C1_ER_IRQHandler();
    }
    else if ((flags & I2C_SR1_SB) != 0U)
    {
        I2C1->DR = 0x78U; // 0x3C, write bit = 0.
    }
    else if ((flags & I2C_SR1_ADDR) != 0U)
    {
        (void)I2C1->SR2; // SR1 read above, then SR2 clears ADDR.
    }
    else if (position < length && (flags & I2C_SR1_TXE) != 0U)
    {
        I2C1->DR = tx[position++];
        if (position == length)
        {
            I2C1->CR2 &= ~I2C_CR2_ITBUFEN;
        }
    }
    else if (position == length && (flags & I2C_SR1_BTF) != 0U)
    {
        I2C1->CR1 |= I2C_CR1_STOP;
        I2C1->CR2 &= ~(I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN | I2C_CR2_ITERREN);
        state = BusStopping;
    }
}
