#include "NovaLink.hpp"
#include "MainFrame.hpp"
#include "LineFollowBoard.hpp"
#include "MotorSpeedProfiles.hpp"
#include "bsp_oled_bus.h"
#include "stm32f1xx_hal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <iostream>

TestDwt test_dwt = {};
TestCoreDebug test_core_debug = {};
uint32_t test_tick = 0, test_primask = 0;
DcMotor Motor1, Motor2, Motor3, Motor4;
MotorProtection MotorSafety(Motor1, Motor2, Motor3, Motor4);
GraySensor GrayArray;
LineFollowApp LineFollow(GrayArray, Motor1, Motor2, Motor3, Motor4);
static std::string input;
static std::vector<std::string> output;
static DcMotor *const motors[] = {&Motor1, &Motor2, &Motor3, &Motor4};
static float rpm = 0, applied_duty = 0;
static bool feedback = true, rx_lost = false, block_tx = false;
static unsigned request_id = 0;
static uint32_t last_feedback = 0;
static bool simulate = false;

extern "C" uint8_t BspUart1Dma_Init(uint32_t baud)
{
    return baud == 115200;
}

extern "C" void BspUart1Dma_Service()
{
}

extern "C" uint16_t BspUart1Dma_Read(uint8_t *data, uint16_t capacity, uint8_t *lost)
{
    *lost = rx_lost;
    rx_lost = false;
    const auto size = input.size() < capacity ? input.size() : capacity;
    memcpy(data, input.data(), size);
    input.erase(0, size);
    return static_cast<uint16_t>(size);
}

extern "C" uint8_t BspUart1Dma_Send(const uint8_t *data, uint16_t size)
{
    if (block_tx)
    {
        return 0;
    }
    output.emplace_back(reinterpret_cast<const char *>(data), size);
    return 1;
}

extern "C" BspUart1Dma_Stats BspUart1Dma_GetStats()
{
    return {};
}

extern "C" uint8_t BspOledBus_Init()
{
    return 0;
}

extern "C" uint8_t BspOledBus_Send(uint8_t, const uint8_t *, uint16_t)
{
    return 0;
}

extern "C" BspOledBus_Status BspOledBus_Poll()
{
    return BspOledBus_Error;
}

static void Tick(unsigned count = 1)
{
    while (count--)
    {
        ++test_tick;
        if (simulate)
        {
            rpm += (applied_duty * 260 - rpm) * .001f / .18f;
        }
        NovaLink.Service();
        if (MotorSafety.BeforeControl())
        {
            DcMotor::ControlAllMotors();
            if (!MotorSafety.AfterControl())
            {
                System.Stop(true);
            }
        }
        else
        {
            System.Stop(true);
        }
        NovaLink.AfterControl();
        assert(!Motor2.IsEnabled() && !Motor3.IsEnabled() && !Motor4.IsEnabled());
    }
}

static std::string Command(const std::string &command, const std::string &fields = "",
                           int channel = 1)
{
    std::string id = "test-" + std::to_string(++request_id);
    input = "@{\"type\":\"command\",\"id\":\"" + id + "\",\"cmd\":\"" + command +
            "\",\"channel\":" + std::to_string(channel) + fields + "}\n";
    size_t before = output.size();
    while (!input.empty())
    {
        Tick();
    }
    for (size_t i = before; i < output.size(); ++i)
    {
        if (output[i].find(id) != std::string::npos)
        {
            return output[i];
        }
    }
    assert(block_tx);
    return {};
}

static bool Ok(const std::string &response)
{
    return response.find("\"ok\":true") != std::string::npos;
}

static void Heartbeat()
{
    // Exact shape produced by the real Nova AutomaticTuner (no type, constant ID).
    input = "@{\"cmd\":\"heartbeat\",\"id\":\"test-heartbeat\",\"channel\":1}\n";
    while (!input.empty())
    {
        Tick();
    }
}

static void Wait(unsigned ms)
{
    for (unsigned n = 0; n < ms; ++n)
    {
        if (n % 200 == 0)
        {
            Heartbeat();
        }
        Tick();
    }
}

static void Configure()
{
    assert(Ok(Command("configure", ",\"actual_min\":-180,\"actual_max\":180,"
                                   "\"output_limit\":0.5,\"watchdog\":1")));
}

int main(int argc, char **argv)
{
    System.Init(false);
    System.BindStopHandler(
        []()
        {
            for (auto m : motors)
            {
                m->Disable();
            }
        });
    for (unsigned i = 0; i < 4; ++i)
    {
        DcMotor::Driver driver = {};
        driver.context = motors[i];
        driver.set_duty = [](void *ctx, float duty)
        {
            if (ctx == &Motor1)
            {
                applied_duty = duty;
            }
            else
            {
                assert(duty == 0);
            }
            return true;
        };
        driver.stop = [](void *ctx)
        {
            if (ctx == &Motor1)
            {
                applied_duty = 0;
            }
        };
        driver.read_rpm = [](void *ctx, float *value)
        {
            if (ctx != &Motor1)
            {
                *value = 0;
                return true;
            }
            if (!feedback || test_tick - last_feedback < 10)
            {
                return false;
            }
            last_feedback = test_tick;
            *value = rpm;
            return true;
        };
        assert(motors[i]->Init(driver));
        assert(MotorSpeedProfiles::Apply(*motors[i], i + 1, false));
    }
    auto config = Motor1.GetSpeedPidConfig();
    config.derivative_on_measurement = true;
    assert(Motor1.ConfigureSpeedPid(config));
    assert(MotorSafety.Init());
    assert(NovaLink.Init());
    Tick(20);
    const std::string scenario = argc > 1 ? argv[1] : "normal";
    if (scenario == "--server")
    {
        // Host-only physical plant model; the protocol/controller/protection are production C++.
        simulate = true;
        output.clear();
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (line == "tick")
            {
                Tick(10);
            }
            else
            {
                input += line + "\n";
                while (!input.empty())
                {
                    Tick();
                }
            }
            for (const auto &frame : output)
            {
                std::cout << frame;
            }
            output.clear();
            std::cout << "END " << test_tick << " " << Motor1.IsEnabled() << std::endl;
        }
        return 0;
    }
    assert(Command("hello").find("positional_pid_d_on_measurement") != std::string::npos);
    assert(Command("hello").find("watchdog") != std::string::npos);
    assert(Command("hello", "", 2).find("watchdog") == std::string::npos);
    assert(!Ok(Command("set_target", ",\"value\":60")));
    assert(!Ok(Command("configure", ",\"actual_min\":-1000,\"actual_max\":1000,"
                                    "\"output_limit\":10,\"watchdog\":5")));
    Configure();
    if (scenario == "pid")
    {
        assert(Ok(Command("set_pid", ",\"gains\":[0,0,0.005]")));
        Wait(1300);
        assert(Ok(Command("set_target", ",\"value\":60")));
        Tick(20);
        assert(fabsf(Motor1.GetDuty()) < .000001f); // D-only: target step has no kick.
        rpm = 3;
        Tick(20);
        assert(Motor1.GetDuty() < 0); // D opposes rising measurement.
        assert(Ok(Command("stop")));
        rpm = 0;
        Wait(1300);
        assert(Ok(Command("set_pid", ",\"gains\":[0.1,1,0]")));
        assert(Ok(Command("set_target", ",\"value\":60")));
        Tick(100);
        assert(fabsf(Motor1.GetDuty() - .5f) < .00001f);
        assert(Ok(Command("set_target", ",\"value\":0.01")));
        Tick(10);
        assert(fabsf(Motor1.GetDuty()) < .002f); // Saturation did not wind up the integrator.
        puts("PASS: measurement derivative, no target kick, output limit and anti-windup");
        return 0;
    }
    assert(!Ok(Command("set_target", ",\"value\":60", 2)));
    assert(!Ok(Command("set_pid", ",\"gains\":[0.002,0.01,0]", 2)));
    assert(Ok(Command("stop")));
    assert(Ok(Command("set_pid", ",\"gains\":[0.0012,0.008,0]")));
    assert(Ok(Command("set_target", ",\"value\":0")));
    assert(!Motor1.IsEnabled());
    assert(Command("set_target", ",\"value\":60").find("waiting_for_standstill") !=
           std::string::npos);
    Wait(1300);
    assert(!Ok(Command("set_target", ",\"value\":151")));
    assert(!Ok(Command("set_target", ",\"value\":-151")));
    assert(Ok(Command("set_target", ",\"value\":150")));
    assert(Motor1.GetTargetSpeed() == 150);
    assert(Ok(Command("set_target", ",\"value\":60")));
    assert(Motor1.IsEnabled());
    assert(!Ok(Command("set_pid", ",\"gains\":[0.002,0.01,0]")));
    assert(!Ok(Command("set_target", ",\"value\":-60")));
    rpm = 60;
    if (scenario == "watchdog")
    {
        Tick(1100);
    }
    else if (scenario == "gap")
    {
        test_tick += 60;
        Tick();
    }
    else if (scenario == "feedback")
    {
        feedback = false;
        Tick(60);
    }
    else if (scenario == "no_motion")
    {
        rpm = 0;
        Wait(1700);
    }
    else if (scenario == "direction")
    {
        rpm = -40;
        Wait(600);
    }
    else if (scenario == "overspeed")
    {
        rpm = 200;
        Tick(20);
    }
    else if (scenario == "uart")
    {
        rx_lost = true;
        Tick();
        input = "\n";
        Tick();
    }
    else if (scenario == "ack")
    {
        block_tx = true;
        Command("get");
        block_tx = false;
        Tick();
    }
    else if (scenario == "duration")
    {
        Wait(45100);
    }
    else if (scenario == "normal")
    {
        Wait(2500); // Repeated identical heartbeat IDs keep the lease alive.
        assert(Motor1.IsEnabled());
        assert(fabsf(Motor1.GetDuty()) <= .5f);
        assert(Ok(Command("stop")));
        assert(!Motor1.IsEnabled() && applied_duty == 0);
        assert(Ok(Command("set_pid", ",\"gains\":[0.002,0.01,0]")));
        assert(Ok(Command("set_target", ",\"value\":0")));
        assert(!Ok(Command("set_target", ",\"value\":60")));
        rpm = 0;
        Wait(1300);
        assert(Ok(Command("set_target", ",\"value\":-60")));
        assert(Motor1.IsEnabled());
        assert(Ok(Command("stop")));
        puts("PASS: M1-only ownership, real Nova heartbeat, limits, neutral and stop interlock");
        return 0;
    }
    else
    {
        assert(false);
    }
    assert(!Motor1.IsEnabled() && applied_duty == 0);
    assert(Ok(Command("stop")));
    rpm = 0;
    Wait(1300);
    assert(!Ok(Command("configure", ",\"actual_min\":-180,\"actual_max\":180,"
                                    "\"output_limit\":0.5,\"watchdog\":1")));
    assert(!Ok(Command("set_target", ",\"value\":60")));
    printf("PASS: %s stop and fault latch preserved\n", scenario.c_str());
}
