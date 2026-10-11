#include "NovaLink.hpp"
#include "MainFrame.hpp"
#include "LineFollowBoard.hpp"
#include "MotorSpeedProfiles.hpp"
#include "MotorContinuousTest.hpp"
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
MotorContinuousTest MotorBench(Motor1, Motor2, Motor3, Motor4, MotorSafety);
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
static bool monitor_mode = false;

static int TestChannel()
{
    return monitor_mode ? MotorContinuousTest::SelectedPort : NovaLinkApp::TuningPort;
}

static DcMotor &TestMotor()
{
    return *motors[TestChannel() - 1];
}

static void CheckInactiveMotors()
{
    for (unsigned i = 0; i < 4; ++i)
    {
        if (i != static_cast<unsigned>(TestChannel() - 1))
        {
            assert(!motors[i]->IsEnabled());
        }
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        if (i != static_cast<unsigned>(TestChannel() - 1))
        {
            const auto pid = motors[i]->GetSpeedPidConfig();
            assert(pid.kp == 0 && pid.ki == 0 && pid.kd == 0);
        }
    }
}

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
        if (monitor_mode)
        {
            if (System.GetState() == Systems::ERROR)
            {
                MotorBench.Stop();
            }
            if (!MotorBench.Run())
            {
                System.Stop(true);
            }
            NovaLink.Service();
            CheckInactiveMotors();
            continue;
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
        CheckInactiveMotors();
    }
}

static std::string Command(const std::string &command, const std::string &fields = "",
                           int channel = TestChannel())
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
    input = "@{\"cmd\":\"heartbeat\",\"id\":\"test-heartbeat\",\"channel\":" +
            std::to_string(TestChannel()) + "}\n";
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
                                   "\"output_limit\":0.69,\"watchdog\":1")));
}

int main(int argc, char **argv)
{
    const std::string scenario = argc > 1 ? argv[1] : "normal";
    monitor_mode = scenario.find("monitor_") == 0;
    const int other_channel = TestChannel() == 2 ? 1 : 2;
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
            if (ctx == &TestMotor())
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
            if (ctx == &TestMotor())
            {
                applied_duty = 0;
            }
        };
        driver.read_rpm = [](void *ctx, float *value)
        {
            if (ctx != &TestMotor())
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
    for (auto motor : motors)
    {
        const auto pid = motor->GetSpeedPidConfig();
        assert(!motor->IsEnabled() && pid.kp == 0 && pid.ki == 0 && pid.kd == 0);
    }
    auto config = TestMotor().GetSpeedPidConfig();
    config.derivative_on_measurement = true;
    // Synthetic host plant gains, not measured hardware parameters; never compiled into firmware.
    if (monitor_mode)
    {
        config.kp = .002f;
        config.ki = .012f;
    }
    assert(TestMotor().ConfigureSpeedPid(config));
    const float expected_kp = config.kp;
    const char *expected_pid_reply =
        monitor_mode ? "PID:0.002000,0.012000,0.000000" : "PID:0.000000,0.000000,0.000000";
    assert(TestMotor().GetSpeedPidConfig().integral_limit == .69f);
    assert(MotorSafety.Init());
    if (monitor_mode)
    {
        assert(MotorBench.Init());
        assert(NovaLink.InitMonitor(TestChannel(),
                                    []()
                                    {
                                        MotorBench.Stop();
                                    }));
    }
    else
    {
        assert(NovaLink.Init());
    }
    Tick(20);
    if (monitor_mode)
    {
        const auto hello = Command("hello");
        assert(hello.find("set_pid") == std::string::npos);
        assert(hello.find("set_target") == std::string::npos);
        assert(hello.find("watchdog") == std::string::npos);
        assert(hello.find("stop") != std::string::npos);
        assert(Ok(Command("get")));
        assert(!Ok(Command("get", "", other_channel)));
        assert(Command("set_pid", ",\"gains\":[0.002,0.01,0]").find("monitor_only") !=
               std::string::npos);
        assert(!Ok(Command("configure", ",\"actual_min\":-180,\"actual_max\":180,"
                                        "\"output_limit\":0.5,\"watchdog\":1")));
        assert(!Ok(Command("set_target", ",\"value\":60")));
        assert(!TestMotor().IsEnabled());
        output.clear();
        input = "LOOP:" + std::to_string(TestChannel()) + "\r\nPID?\r\nPID:Kp=0.002\r\n";
        Tick(20);
        std::string replies;
        for (const auto &frame : output)
        {
            replies += frame;
        }
        assert(replies.find("OK:LOOP:" + std::to_string(TestChannel())) != std::string::npos);
        assert(replies.find(expected_pid_reply) != std::string::npos);
        assert(replies.find("ERR:monitor_only") != std::string::npos);
        assert(TestMotor().GetSpeedPidConfig().kp == expected_kp);
        assert(TestMotor().GetSpeedPidConfig().integral_limit == 0.69f);
        if (scenario == "monitor_stop_wait")
        {
            assert(Ok(Command("stop")));
            Tick(5000);
            assert(!TestMotor().IsEnabled());
            assert(MotorBench.GetStatus().failure == MotorContinuousTest::Failure::ExternalStop);
            puts("PASS: monitor STOP before startup prevents later automatic motion");
            return 0;
        }
        if (scenario == "monitor_no_motion")
        {
            Tick(3600);
            assert(MotorSafety.GetStatus().fault == MotorProtection::Fault::NoMotion);
            assert(MotorSafety.GetStatus().failed_port == MotorContinuousTest::SelectedPort);
            Tick(2000);
            assert(!TestMotor().IsEnabled());
            puts("PASS: telemetry monitoring preserves NoMotion fault and latch");
            return 0;
        }
        simulate = true;
        Tick(60000); // No configure or heartbeat; continuous local movement remains the owner.
        assert(TestMotor().IsEnabled() && TestMotor().GetTargetSpeed() == 100);
        assert(fabsf(TestMotor().GetFilteredRpm() - 100) < 2);
        assert(fabsf(TestMotor().GetDuty()) <= .69001f);
        output.clear();
        Tick(1000);
        unsigned frames = 0;
        for (const auto &frame : output)
        {
            if (frame.find("\"type\":\"telemetry\"") != std::string::npos)
            {
                ++frames;
                assert(frame.find("\"channel\":" + std::to_string(TestChannel())) !=
                       std::string::npos);
                assert(frame.find("\"sp\":100.000000") != std::string::npos);
                assert(frame.find("\"pv\":0.000000") == std::string::npos);
            }
        }
        assert(frames == 50);
        block_tx = true;
        rx_lost = true;
        Tick(3000);
        assert(TestMotor().IsEnabled() && TestMotor().GetTargetSpeed() == 100);
        block_tx = false;
        input = "\n"; // Complete resynchronization after simulated transport loss.
        Tick();
        assert(!Ok(Command("set_pid", ",\"gains\":[0.002,0.01,0]")));
        assert(!Ok(Command("set_target", ",\"value\":60")));
        assert(TestMotor().GetSpeedPidConfig().kp == expected_kp &&
               TestMotor().GetTargetSpeed() == 100);
        assert(Ok(Command("stop")));
        Tick(3000);
        assert(!TestMotor().IsEnabled());
        assert(MotorBench.GetStatus().failure == MotorContinuousTest::Failure::ExternalStop);
        puts("PASS: 50 Hz live RPM, read-only legacy/JSON access, TX/RX loss, continuous run and "
             "STOP");
        return 0;
    }
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
            std::cout << "END " << test_tick << " " << TestMotor().IsEnabled() << std::endl;
        }
        return 0;
    }
    assert(Command("hello").find("positional_pid_d_on_measurement") != std::string::npos);
    assert(Command("hello").find("watchdog") != std::string::npos);
    assert(Command("hello", "", other_channel).find("watchdog") == std::string::npos);
    assert(!Ok(Command("set_target", ",\"value\":60")));
    assert(!Ok(Command("configure", ",\"actual_min\":-1000,\"actual_max\":1000,"
                                    "\"output_limit\":10,\"watchdog\":5")));
    assert(!Ok(Command("configure", ",\"actual_min\":-180,\"actual_max\":180,"
                                    "\"output_limit\":0.70,\"watchdog\":1")));
    for (int channel = 1; channel <= 5; ++channel)
    {
        if (channel == TestChannel())
        {
            continue;
        }
        assert(Command("hello", "", channel).find("watchdog") == std::string::npos);
        assert(Command("hello", "", channel).find("set_pid") == std::string::npos);
        assert(!Ok(Command("set_pid", ",\"gains\":[0.002,0.01,0]", channel)));
        assert(!Ok(Command("set_target", ",\"value\":100", channel)));
        assert(!Ok(Command("configure",
                           ",\"actual_min\":-120,\"actual_max\":150,"
                           "\"output_limit\":0.69,\"watchdog\":1",
                           channel)));
    }
    Configure();
    assert(TestMotor().GetSpeedPidConfig().integral_limit == 0.69f);
    if (scenario == "pid")
    {
        assert(Ok(Command("set_pid", ",\"gains\":[0,0,0.005]")));
        Wait(1300);
        assert(Ok(Command("set_target", ",\"value\":60")));
        Tick(20);
        assert(fabsf(TestMotor().GetDuty()) < .000001f); // D-only: target step has no kick.
        rpm = 3;
        Tick(20);
        assert(TestMotor().GetDuty() < 0); // D opposes rising measurement.
        assert(Ok(Command("stop")));
        rpm = 0;
        Wait(1300);
        assert(Ok(Command("set_pid", ",\"gains\":[0.1,1,0]")));
        assert(Ok(Command("set_target", ",\"value\":60")));
        Tick(100);
        assert(fabsf(TestMotor().GetDuty() - .69f) < .00001f);
        assert(Ok(Command("set_target", ",\"value\":0.01")));
        Tick(10);
        assert(fabsf(TestMotor().GetDuty()) < .002f); // Saturation did not wind up the integrator.
        puts("PASS: measurement derivative, no target kick, output limit and anti-windup");
        return 0;
    }
    assert(!Ok(Command("set_target", ",\"value\":60", other_channel)));
    assert(!Ok(Command("set_pid", ",\"gains\":[0.002,0.01,0]", other_channel)));
    assert(Ok(Command("stop")));
    assert(Ok(Command("set_pid", ",\"gains\":[0.002,0.012,0]")));
    assert(Ok(Command("set_target", ",\"value\":0")));
    assert(!TestMotor().IsEnabled());
    assert(Command("set_target", ",\"value\":60").find("waiting_for_standstill") !=
           std::string::npos);
    Wait(1300);
    assert(!Ok(Command("set_target", ",\"value\":151")));
    assert(!Ok(Command("set_target", ",\"value\":-151")));
    assert(Ok(Command("set_target", ",\"value\":150")));
    assert(TestMotor().GetTargetSpeed() == 150);
    assert(Ok(Command("set_target", ",\"value\":60")));
    assert(TestMotor().IsEnabled());
    assert(!Ok(Command("set_pid", ",\"gains\":[0.002,0.01,0]")));
    assert(!Ok(Command("set_target", ",\"value\":-60")));
    rpm = 60;
    if (scenario == "other_enabled")
    {
        auto &other = *motors[TestChannel() == 1 ? 2 : 0];
        assert(other.SwitchMode(DcMotor::Mode::Duty) && other.Enable());
        Tick();
    }
    else if (scenario == "watchdog")
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
        assert(TestMotor().IsEnabled());
        assert(fabsf(TestMotor().GetDuty()) <= .69f);
        assert(Ok(Command("stop")));
        assert(!TestMotor().IsEnabled() && applied_duty == 0);
        assert(Ok(Command("set_pid", ",\"gains\":[0.002,0.01,0]")));
        assert(Ok(Command("set_target", ",\"value\":0")));
        assert(!Ok(Command("set_target", ",\"value\":60")));
        rpm = 0;
        Wait(1300);
        assert(Ok(Command("set_target", ",\"value\":-60")));
        assert(TestMotor().IsEnabled());
        assert(Ok(Command("stop")));
        printf(
            "PASS: M%d-only ownership, real Nova heartbeat, limits, neutral and stop interlock\n",
            TestChannel());
        return 0;
    }
    else
    {
        assert(false);
    }
    assert(!TestMotor().IsEnabled() && applied_duty == 0);
    if (scenario == "no_motion")
    {
        assert(MotorSafety.GetStatus().failed_port == TestChannel());
    }
    bool channel_fault = false;
    for (const auto &frame : output)
    {
        if (frame.find("\"type\":\"fault\"") != std::string::npos &&
            frame.find("\"channel\":" + std::to_string(TestChannel())) != std::string::npos)
        {
            channel_fault = true;
        }
    }
    assert(channel_fault);
    assert(Ok(Command("stop")));
    rpm = 0;
    Wait(1300);
    assert(!Ok(Command("configure", ",\"actual_min\":-180,\"actual_max\":180,"
                                    "\"output_limit\":0.5,\"watchdog\":1")));
    assert(!Ok(Command("set_target", ",\"value\":60")));
    printf("PASS: %s stop and fault latch preserved\n", scenario.c_str());
}
