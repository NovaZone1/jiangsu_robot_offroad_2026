#include "RangeDisplayApp.hpp"
#include "ultrasonic_factory.hpp"
#include "System.hpp"
#include "stm32f1xx_hal.h"
#include <math.h>
#include <stdio.h>

RangeDisplayApp RangeDisplay;

void RangeDisplayApp::Start()
{
    if (!display_.Init())
    {
        System.monitor.LogWarning("Factory OLED initialization failed");
    }
    display_.SetLine(0, "ULTRASONIC");
    display_.SetLine(1, "---.- CM");
    display_.SetLine(3, "WAITING");
    last_frame_ms_ = HAL_GetTick();
}

App::Status RangeDisplayApp::GetStatus()
{
    // Display is diagnostic, not a prerequisite for the existing vehicle self-check.
    return display_.IsHealthy() ? App::Normal : App::Warning;
}

void RangeDisplayApp::Update()
{
    display_.Update();
    uint32_t now = HAL_GetTick();
    if (display_.IsBusy() || (uint32_t)(now - last_frame_ms_) < 200U)
    {
        return;
    }
    last_frame_ms_ = now;
    float cm = -1.0f;
    FactoryUltrasonic::Status result = FactoryUltrasonic::ReadDistanceCm(cm);
    const char *label = "INVALID";
    char text[22] = "---.- CM";
    using FactoryUltrasonic::Status;
    switch (result)
    {
    case Status::Ok:
        if (isfinite(cm) && cm > 0.0f && cm <= 500.0f)
        {
            uint32_t tenths = (uint32_t)(cm * 10.0f + 0.5f);
            snprintf(text, sizeof(text), "%lu.%lu CM", (unsigned long)(tenths / 10U),
                     (unsigned long)(tenths % 10U));
            label = "OK";
        }
        break;
    case Status::NotInitialized:
        label = "NOT INIT";
        break;
    case Status::Waiting:
        label = "WAITING";
        break;
    case Status::Timeout:
        label = "TIMEOUT";
        break;
    case Status::HardwareError:
        label = "HW ERROR";
        break;
    case Status::Stale:
        label = "STALE";
        break;
    case Status::InvalidEcho:
        break;
    }
    display_.SetLine(1, text);
    display_.SetLine(3, label);
    display_.Refresh();
}
