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
static float rpm;
static_assert(!NovaLinkApp::MotorTuningEnabled, "Production must default to disabled AI trials");

extern "C" uint8_t BspUart1Dma_Init(uint32_t baud)
{
    return baud == 115200;
}

extern "C" void BspUart1Dma_Service()
{
}

extern "C" uint16_t BspUart1Dma_Read(uint8_t *data, uint16_t capacity, uint8_t *lost)
{
    *lost = 0;
    const auto size = input.size() < capacity ? input.size() : capacity;
    memcpy(data, input.data(), size);
    input.erase(0, size);
    return static_cast<uint16_t>(size);
}

extern "C" uint8_t BspUart1Dma_Send(const uint8_t *data, uint16_t size)
{
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

static std::string Command(const std::string &line)
{
    input = line + "\n";
    const size_t before = output.size();
    while (!input.empty())
    {
        ++test_tick;
        DcMotor::ControlAllMotors();
        NovaLink.Service();
    }
    for (size_t i = before; i < output.size(); ++i)
    {
        if (output[i].find("\"type\":\"telemetry\"") == std::string::npos &&
            output[i].find("\"type\":\"fault\"") == std::string::npos)
        {
            return output[i];
        }
    }
    assert(false);
    return {};
}

int main()
{
    System.Init(false);
    DcMotor::Driver driver = {};
    driver.set_duty = [](void *, float)
    {
        return true;
    };
    driver.stop = [](void *)
    {
    };
    driver.read_rpm = [](void *, float *value)
    {
        *value = rpm;
        return true;
    };
    const float expected_kp[] = {.000957f, .001329f, .001176f, .001442f};
    const float expected_ki[] = {.009191f, .010857f, .013926f, .010392f};
    for (uint8_t i = 0; i < 4; ++i)
    {
        assert(motors[i]->Init(driver));
        assert(MotorSpeedProfiles::Apply(*motors[i], i + 1U, false));
        const auto pid = motors[i]->GetSpeedPidConfig();
        assert(pid.kp == expected_kp[i] && pid.ki == expected_ki[i] && pid.kd == 0);
        assert(pid.integral_limit == .69f && !motors[i]->IsEnabled());
    }
    assert(MotorSafety.Init());
    assert(LineFollow.Init(LineFollowApp::Config()));
    assert(NovaLink.Init());
    auto response =
        Command("@{\"type\":\"command\",\"id\":\"t-1\",\"cmd\":\"hello\",\"channel\":1}");
    assert(response.find("positional_pid_d_on_error") != std::string::npos);
    assert(response.find("watchdog") == std::string::npos &&
           response.find("set_target") == std::string::npos);
    response = Command(
        "@{\"type\":\"command\",\"id\":\"t-2\",\"cmd\":\"set_target\",\"channel\":1,\"value\":60}");
    assert(response.find("selected_motor_tuning_only") != std::string::npos);
    assert(!Motor1.IsEnabled());
    for (uint8_t port = 1; port <= 4; ++port)
    {
        const std::string channel = std::to_string(port);
        response = Command("@{\"type\":\"command\",\"id\":\"idle-" + channel +
                           "\",\"cmd\":\"set_target\",\"channel\":" + channel + ",\"value\":100}");
        assert(response.find("selected_motor_tuning_only") != std::string::npos);
        for (auto motor : motors)
        {
            assert(!motor->IsEnabled() && motor->GetDuty() == 0);
        }
    }
    assert(Command("LOOP:2") == "OK:LOOP:2\r\n");
    assert(Command("PID:0.002,0.01,0.0001") == "OK:PID\r\n");
    assert(Motor2.GetSpeedPidConfig().kp == .002f && Motor1.GetSpeedPidConfig().kp == .000957f);
    assert(Motor2.GetSpeedPidConfig().feedback_filter_tau_s == .03f);
    assert(Command("PID:Kp=1").find("gain_limit") != std::string::npos);
    assert(Motor2.Enable());
    assert(Command("PID:Ki=0.02").find("stop_and_disable_first") != std::string::npos);
    assert(Motor2.GetSpeedPidConfig().ki == .01f);
    response = Command("@{\"type\":\"command\",\"id\":\"t-3\",\"cmd\":\"stop\",\"channel\":2}");
    assert(response.find("\"ok\":true") != std::string::npos);
    for (auto motor : motors)
    {
        assert(!motor->IsEnabled());
    }
    assert(Command("LOOP:5") == "OK:LOOP:5\r\n");
    assert(Command("PID:30,1,0.2") == "OK:PID\r\n");
    assert(LineFollow.GetTrackConfig().ki == 1);
    assert(LineFollow.GetTrackConfig().base_rpm == 40);
    assert(Command("PID:Ki=101").find("invalid_PID") != std::string::npos);
    assert(LineFollow.GetTrackConfig().ki == 1);
    Track track;
    Track::Config config;
    config.kp = config.kd = 0;
    config.ki = 100;
    assert(track.Init(config));
    GraySensor::Sample sample = {};
    sample.channels = 8;
    sample.valid = true;
    sample.line[7] = 1;
    for (unsigned i = 1; i <= 200; ++i)
    {
        sample.timestamp_ms = i * 5;
        track.Update(sample, sample.timestamp_ms);
    }
    assert(fabsf(track.GetCommand().speed_diff_rpm - config.integral_limit_rpm) < .001f);
    assert(track.GetCommand().timestamp_ms == sample.timestamp_ms);
    System.Stop(true);
    response = Command("@{\"type\":\"command\",\"id\":\"t-4\",\"cmd\":\"stop\",\"channel\":5}");
    assert(System.GetState() == Systems::ERROR);
    assert(Motor1.Enable());
    rpm = 500;
    ++test_tick;
    DcMotor::ControlAllMotors();
    assert(!MotorSafety.AfterControl());
    const auto fault = MotorSafety.GetStatus().fault;
    Command("@{\"type\":\"command\",\"id\":\"t-5\",\"cmd\":\"stop\",\"channel\":1}");
    assert(MotorSafety.GetStatus().fault == fault);
    assert(Command("PID:Ki=2").find("motor_protection_latched") != std::string::npos);
    puts("PASS: Nova real motor/track binding, limits, stopped-only writes, no remote start, "
         "integral bound and protection latch preservation");
}
