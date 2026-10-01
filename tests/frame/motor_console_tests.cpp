#include "bsp_motor_console.h"
#include <assert.h>
#include <stdio.h>
#include <string>

USART_TypeDef test_usart1 = {};
bool test_uart_init_failure = false;

namespace
{
    std::string Drain()
    {
        std::string output;
        for (unsigned attempt = 0; attempt < 768; ++attempt)
        {
            test_usart1.SR = UART_FLAG_TXE;
            BspMotorConsole_Pump();
            if (test_usart1.SR & UART_FLAG_TXE)
            {
                return output; // 队列空，未尝试写串口。
            }
            output += (char)test_usart1.DR;
        }
        assert(false);
        return output;
    }
} // namespace

void RunMotorConsoleTests()
{
    assert(BspMotorConsole_WriteLine("not initialized") == HAL_ERROR);
    test_uart_init_failure = true;
    assert(BspMotorConsole_Init() == HAL_ERROR);
    test_uart_init_failure = false;
    assert(BspMotorConsole_Init() == HAL_OK);
    assert(BspMotorConsole_WriteLine(nullptr) == HAL_ERROR);
    assert(BspMotorConsole_WriteLine("BOOT") == HAL_OK);
    test_usart1.SR = 0;
    test_usart1.DR = 0xAA;
    BspMotorConsole_Pump();
    assert(test_usart1.DR == 0xAA); // 串口忙时不等待、不消费队列。
    assert(Drain() == "BOOT\r\n");
    assert(BspMotorConsole_WriteLine("FIRST") == HAL_OK);
    const std::string oversized(768, 'x');
    assert(BspMotorConsole_WriteLine(oversized.c_str()) == HAL_BUSY);
    assert(Drain() == "FIRST\r\n");
    const std::string large(700, 'a');
    assert(BspMotorConsole_WriteLine(large.c_str()) == HAL_OK);
    assert(BspMotorConsole_WriteLine(std::string(80, 'b').c_str()) == HAL_BUSY);
    assert(Drain() == large + "\r\n");
    for (unsigned index = 0; index < 300; ++index)
    {
        assert(BspMotorConsole_WriteLine("STATE") == HAL_OK);
        assert(Drain() == "STATE\r\n"); // 多次跨过环形队列边界。
    }
    puts("PASS: motor UART diagnostic queue, full-line overflow and nonblocking transmit");
}
