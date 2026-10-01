#pragma once

#include "Application.hpp"
#include "Monitor.hpp"
#include "SysDefs.hpp"
#include "frame_config.h"

namespace Systems
{
    enum SystemState
    {
        ORIGIN,
        SELF_CHECK,
        READY,
        WORKING,
        ERROR,
        STOP
    };
} // namespace Systems

class RobotSystem
{
    SINGLETON(RobotSystem) {};

public:
    using Indicator = void (*)(bool on);
    using StopHandler = void (*)();

    Monitor &monitor = Monitor::GetInstance();
    float runtime_tick = 0;
    bool start_selfcheck_flag = false;
    bool system_ready_flag = false;
    bool system_start_to_work_flag = false;

    void Init(bool self_check = true);
    bool RegistApp(Application &app);
    Application *FindApp(const char *name);

    void Run();
    void Working();
    void UpdateApplications();
    void Stop(bool error = false);

    void PerformanceRun()
    {
    }

    Systems::SystemState GetState() const
    {
        return state_;
    }

    void BindIndicator(Indicator indicator)
    {
        indicator_ = indicator;
    }

    void BindStopHandler(StopHandler stop)
    {
        stop_ = stop;
    }

private:
    bool AreApplicationsReady();
    void UpdateSelfCheck();
    void UpdateApplicationHealth();
    void UpdateIndicator();

    Systems::SystemState state_ = Systems::ORIGIN;
    Application *apps_[FRAME_MAX_APPLICATIONS] = {};
    uint8_t count_ = 0;
    Indicator indicator_ = nullptr;
    StopHandler stop_ = nullptr;
};

// 保留全局访问入口；构造阶段不操作外设。
extern RobotSystem &System;