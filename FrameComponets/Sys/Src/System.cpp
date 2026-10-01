#include "System.hpp"

#include "Action.hpp"
#include "StateCore.hpp"
#include "bsp_dwt.h"
#include "stm32f1xx_hal.h"
#include <string.h>

namespace
{
    constexpr uint32_t kIndicatorPeriodMs = 500U;
    constexpr uint32_t kIndicatorOnTimeMs = 250U;
} // namespace

RobotSystem &System = RobotSystem::GetInstance();

void RobotSystem::Init(bool self_check)
{
    BspDwt_Init(HAL_RCC_GetHCLKFreq() / 1000000U);
    Action.Init();
    state_ = self_check ? Systems::SELF_CHECK : Systems::ORIGIN;
    system_ready_flag = false;
    system_start_to_work_flag = false;
    start_selfcheck_flag = false;
}

bool RobotSystem::RegistApp(Application &app)
{
    if (count_ >= FRAME_MAX_APPLICATIONS)
    {
        return false;
    }

    for (uint8_t i = 0; i < count_; ++i)
    {
        if (apps_[i] == &app || strcmp(apps_[i]->GetName(), app.GetName()) == 0)
        {
            return false;
        }
    }

    apps_[count_++] = &app;
    app.Start();
    return true;
}

Application *RobotSystem::FindApp(const char *name)
{
    if (!name)
    {
        return nullptr;
    }

    for (uint8_t i = 0; i < count_; ++i)
    {
        if (strcmp(apps_[i]->GetName(), name) == 0)
        {
            return apps_[i];
        }
    }
    return nullptr;
}

bool RobotSystem::AreApplicationsReady()
{
    if (count_ == 0)
    {
        return false;
    }

    for (uint8_t i = 0; i < count_; ++i)
    {
        if (!apps_[i]->WatchPoint())
        {
            return false;
        }
    }
    return true;
}

void RobotSystem::Working()
{
    if (state_ != Systems::READY || !system_start_to_work_flag)
    {
        return;
    }

    const bool ready = AreApplicationsReady();
    system_start_to_work_flag = false;
    if (!ready)
    {
        Stop(true);
        return;
    }

    state_ = Systems::WORKING;
    monitor.LogOK("System working");
}

void RobotSystem::Stop(bool error)
{
    state_ = error ? Systems::ERROR : Systems::STOP;
    system_ready_flag = false;
    system_start_to_work_flag = false;
    StateCore::GetInstance().Disable();
    Action.CancelAll();
    if (stop_)
    {
        stop_();
    }
}

void RobotSystem::UpdateSelfCheck()
{
    if (state_ == Systems::ORIGIN && start_selfcheck_flag)
    {
        start_selfcheck_flag = false;
        state_ = Systems::SELF_CHECK;
    }

    if (state_ == Systems::SELF_CHECK && AreApplicationsReady())
    {
        state_ = Systems::READY;
        system_ready_flag = true;
    }
}

void RobotSystem::UpdateApplicationHealth()
{
    for (uint8_t i = 0; i < count_; ++i)
    {
        if (apps_[i]->status == App::Error && state_ != Systems::ERROR)
        {
            Stop(true);
        }
    }
}

void RobotSystem::UpdateIndicator()
{
    if (!indicator_)
    {
        return;
    }

    // 比赛要求：准备完成后常亮，行驶时有节奏闪烁。
    const bool ready = state_ == Systems::READY;
    const bool blinking =
        state_ == Systems::WORKING && (HAL_GetTick() % kIndicatorPeriodMs) < kIndicatorOnTimeMs;
    indicator_(ready || blinking);
}

void RobotSystem::Run()
{
    runtime_tick = BspDwt_GetTimeline_Sec();
    UpdateSelfCheck();
    Working();
    UpdateApplicationHealth();
    UpdateIndicator();
    monitor.Run();
}

void RobotSystem::UpdateApplications()
{
    for (uint8_t i = 0; i < count_; ++i)
    {
        Application &app = *apps_[i];
        if (!app.CntFull())
        {
            continue;
        }

        app.status = app.GetStatus();
        if (app.status == App::Error)
        {
            Stop(true);
            continue;
        }
        app.Update();
    }
}