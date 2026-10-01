#include "bsp_motor_console.h"
#include <string.h>

#define CONSOLE_CAPACITY 768U

static UART_HandleTypeDef console;
static uint8_t buffer[CONSOLE_CAPACITY];
static uint16_t head, tail;
static uint8_t initialized;

HAL_StatusTypeDef BspMotorConsole_Init(void)
{
    if (initialized)
    {
        return HAL_OK;
    }
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef gpio;
    memset(&gpio, 0, sizeof(gpio));
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    console.Instance = USART1;
    console.Init.BaudRate = 115200;
    console.Init.WordLength = UART_WORDLENGTH_8B;
    console.Init.StopBits = UART_STOPBITS_1;
    console.Init.Parity = UART_PARITY_NONE;
    console.Init.Mode = UART_MODE_TX;
    console.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    console.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&console) != HAL_OK)
    {
        return HAL_ERROR;
    }
    head = tail = 0;
    initialized = 1;
    return HAL_OK;
}

HAL_StatusTypeDef BspMotorConsole_WriteLine(const char *line)
{
    if (!initialized || !line)
    {
        return HAL_ERROR;
    }
    const size_t length = strlen(line);
    const uint16_t free_bytes = (tail + CONSOLE_CAPACITY - head - 1U) % CONSOLE_CAPACITY;
    if (length > CONSOLE_CAPACITY - 3U || length + 2U > free_bytes)
    {
        return HAL_BUSY; // 丢弃整行，不等待串口，不覆盖尚未发送的内容。
    }
    for (size_t index = 0; index < length; ++index)
    {
        buffer[head] = (uint8_t)line[index];
        head = (head + 1U) % CONSOLE_CAPACITY;
    }
    buffer[head] = '\r';
    head = (head + 1U) % CONSOLE_CAPACITY;
    buffer[head] = '\n';
    head = (head + 1U) % CONSOLE_CAPACITY;
    return HAL_OK;
}

void BspMotorConsole_Pump(void)
{
    // 寄存器轮询只负责发送；不调用阻塞 HAL、不使用共享 UART HAL 回调。
    for (uint8_t budget = 0; initialized && tail != head && budget < 16; ++budget)
    {
        if (__HAL_UART_GET_FLAG(&console, UART_FLAG_TXE) == RESET)
        {
            return;
        }
        console.Instance->DR = buffer[tail];
        tail = (tail + 1U) % CONSOLE_CAPACITY;
    }
}
