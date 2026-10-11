#include "stm32f1xx_hal.h"
#include "bluetooth.hpp"
#include <assert.h>
#include <stdio.h>
#include <string>

TestUart test_uart = {};
TestDma test_dma = {};
TestDmaChannel test_tx = {}, test_rx = {};
TestRcc test_rcc = {};
TestAfio test_afio = {};
GPIO_TypeDef test_gpio = {};
static uint32_t tick, mask;

uint32_t HAL_RCC_GetPCLK2Freq()
{
    return 72000000;
}

uint32_t HAL_GetTick()
{
    return tick;
}

uint32_t __get_PRIMASK()
{
    return mask;
}

void __disable_irq()
{
    mask = 1;
}

void __set_PRIMASK(uint32_t value)
{
    mask = value;
}

void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *)
{
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

void HAL_NVIC_ClearPendingIRQ(IRQn_Type)
{
}

void HAL_NVIC_SetPriority(IRQn_Type, uint32_t priority, uint32_t)
{
    assert(priority == 7);
}

void HAL_NVIC_EnableIRQ(IRQn_Type)
{
}

static void Receive(const std::string &bytes)
{
    auto *dma = reinterpret_cast<uint8_t *>(test_rx.CMAR);
    for (unsigned char byte : bytes)
    {
        assert(test_rx.CCR & DMA_CCR_EN);
        unsigned position = 256 - test_rx.CNDTR;
        dma[position] = byte;
        --test_rx.CNDTR;
        if (test_rx.CNDTR == 128 || test_rx.CNDTR == 0)
        {
            test_dma.ISR = test_rx.CNDTR ? DMA_ISR_HTIF5 : DMA_ISR_TCIF5;
            if (!test_rx.CNDTR)
            {
                test_rx.CNDTR = 256;
            }
            DMA1_Channel5_IRQHandler();
            test_dma.ISR = 0; // W1C register emulation at the interrupt boundary.
        }
    }
}

static std::string Drain(Bluetooth &bt, bool expected_loss)
{
    bt.Service();
    uint8_t buffer[64];
    bool lost = false;
    auto size = bt.Read(buffer, sizeof(buffer), lost);
    assert(lost == expected_loss);
    std::string result(reinterpret_cast<char *>(buffer), size);
    while ((size = bt.Read(buffer, sizeof(buffer), lost)))
    {
        assert(!lost);
        result.append(reinterpret_cast<char *>(buffer), size);
    }
    return result;
}

static std::string CompleteTx()
{
    assert(test_tx.CCR & DMA_CCR_EN);
    std::string result(reinterpret_cast<char *>(test_tx.CMAR), test_tx.CNDTR);
    test_dma.ISR = DMA_ISR_TCIF4;
    DMA1_Channel4_IRQHandler();
    test_dma.ISR = 0;
    return result;
}

int main()
{
    Bluetooth bt;
    assert(!bt.Init(0));
    test_afio.MAPR = AFIO_MAPR_USART1_REMAP;
    assert(!bt.Init());
    test_afio.MAPR = 0;
    test_tx.CCR = DMA_CCR_EN;
    assert(!bt.Init());
    test_tx.CCR = 0;
    test_rcc.APB2ENR = RCC_APB2ENR_USART1EN;
    assert(!bt.Init());
    test_rcc.APB2ENR = 0;
    assert(bt.Init() && bt.Init());
    assert(test_uart.BRR == 625 && (test_rx.CCR & DMA_CCR_CIRC));
    assert((test_uart.CR3 & (USART_CR3_DMAR | USART_CR3_DMAT)) ==
           (USART_CR3_DMAR | USART_CR3_DMAT));
    Receive("small partial frame");
    assert(Drain(bt, false) == "small partial frame");
    std::string pattern;
    for (unsigned i = 0; i < 400; ++i)
    {
        pattern += char('A' + i % 26);
    }
    Receive(pattern);
    assert(Drain(bt, false) == pattern); // HT, TC and NDTR polling must not duplicate bytes.
    Receive(std::string(700, 'x'));
    Drain(bt, true);
    assert(bt.GetStats().rx_overflows == 1);
    Receive("new frame\n");
    assert(Drain(bt, false) == "new frame\n");
    Receive("damaged");
    test_uart.SR = USART_SR_ORE;
    USART1_IRQHandler();
    test_uart.SR = 0;
    assert(Drain(bt, true).empty());
    Receive("after uart error\n");
    assert(Drain(bt, false) == "after uart error\n");
    test_dma.ISR = DMA_ISR_TEIF5;
    DMA1_Channel5_IRQHandler();
    test_dma.ISR = 0;
    assert(Drain(bt, true).empty());
    assert(bt.GetStats().rx_errors == 2);
    test_dma.ISR = DMA_ISR_HTIF5 | DMA_ISR_TCIF5;
    DMA1_Channel5_IRQHandler();
    test_dma.ISR = 0;
    Drain(bt, true);
    assert(bt.GetStats().rx_overflows == 2);
    uint8_t payload[] = {'o', 'k', '\n'};
    for (int i = 0; i < 4; ++i)
    {
        assert(bt.Send(payload, 3));
    }
    assert(!bt.Send(payload, 3));
    payload[0] = 'X';
    for (int i = 0; i < 4; ++i)
    {
        bt.Service();
        assert(CompleteTx() == "ok\n");
    }
    assert(bt.Send(payload, 3));
    bt.Service();
    test_dma.ISR = DMA_ISR_TEIF4;
    DMA1_Channel4_IRQHandler();
    test_dma.ISR = 0;
    bt.Service();
    assert(CompleteTx() == "\n");
    assert(bt.Send(payload, 3));
    bt.Service();
    assert(CompleteTx() == "Xk\n");
    assert(bt.Send(payload, 3));
    bt.Service();
    tick += 501;
    bt.Service();
    assert(CompleteTx() == "\n");
    assert(bt.GetStats().tx_errors == 2 && bt.GetStats().tx_rejected == 1 && mask == 0);
    puts("PASS: USART1 circular DMA ordering/wrap/overflow, UART/DMA recovery, copied TX queue, "
         "saturation and timeout");
}
