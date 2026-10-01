#pragma once
#include "../ultrasonic_mocks/stm32f1xx_hal.h"

typedef struct
{
    volatile uint32_t CR1, CR2, OAR1, CCR, TRISE, DR, SR1, SR2;
} OledTestI2c;
typedef struct
{
    uint32_t MAPR;
} OledTestAfio;
#ifdef __cplusplus
extern "C"
{
#endif
    extern OledTestI2c test_i2c;
    extern OledTestAfio test_afio;
#ifdef __cplusplus
}
#endif
#define GPIOB (&test_gpio)
#define I2C1 (&test_i2c)
#define AFIO (&test_afio)
#define GPIO_PIN_6 (1U << 6)
#define GPIO_PIN_7 (1U << 7)
#define GPIO_MODE_AF_OD 4U
#define RCC_APB1ENR_I2C1EN (1U << 21)
#define AFIO_MAPR_I2C1_REMAP (1U << 1)
#define I2C_CR1_PE 1U
#define I2C_CR1_START (1U << 8)
#define I2C_CR1_STOP (1U << 9)
#define I2C_CR1_SWRST (1U << 15)
#define I2C_CR2_ITERREN (1U << 8)
#define I2C_CR2_ITEVTEN (1U << 9)
#define I2C_CR2_ITBUFEN (1U << 10)
#define I2C_CCR_FS (1U << 15)
#define I2C_SR1_SB 1U
#define I2C_SR1_ADDR (1U << 1)
#define I2C_SR1_BTF (1U << 2)
#define I2C_SR1_TXE (1U << 7)
#define I2C_SR1_BERR (1U << 8)
#define I2C_SR1_ARLO (1U << 9)
#define I2C_SR1_AF (1U << 10)
#define I2C_SR1_OVR (1U << 11)
#define I2C_SR2_BUSY (1U << 1)
#define I2C1_EV_IRQn 31
#define I2C1_ER_IRQn 32
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_I2C1_CLK_ENABLE() (test_rcc.APB1ENR |= RCC_APB1ENR_I2C1EN)
