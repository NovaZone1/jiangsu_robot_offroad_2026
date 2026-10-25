#include "RtosCpp.hpp"
#include "System.hpp"
#include "StateCore.hpp"
#include "Action.hpp"
#include "MainFrame.hpp"
#include "LineFollowBoard.hpp"
#include "bsp_dwt.h"
#include "cmsis_os.h"
#include "NovaLink.hpp"
#include "LegacyLineRun.hpp"

void MainInitCpp(void)
{
    System.Init();
    MainFrameCpp();
}

void FrameTickCpp(void)
{
    static uint8_t prescaler = 0;
    BspDwt_CntUpdate();
    if (LegacyLineRun::Enabled)
    {
        LegacyLine.Run();
        return; // One owner, with its own pre/post control protection checks.
    }
    NovaLink.Service();
    if (!MotorSafety.BeforeControl())
    {
        System.Stop(true);
        NovaLink.AfterControl();
        return;
    }
    if (NovaLinkApp::MotorTuningEnabled)
    {
        // Exclusive bench ownership: skip competition updates, start requests and wheel writers.
        // Normal driver feedback/control and all original motor protection remain active.
        DcMotor::ControlAllMotors();
        if (!MotorSafety.AfterControl())
        {
            System.Stop(true);
        }
        NovaLink.AfterControl();
        return;
    }
    if (MotorSafety.IsCoasting())
    {
        System.Stop();
    }
    Offroad.SampleSensors();
    if (++prescaler >= FRAME_SYSTEM_PERIOD_MS / FRAME_CONTROL_PERIOD_MS)
    {
        prescaler = 0;
        System.UpdateApplications();
        System.Run();
        if (System.GetState() == Systems::WORKING)
        {
            StateCore::GetInstance().Run();
            Action.ExecutorRun();
        }
    }
    if (LineFollowBoard::Enabled)
    {
        LineFollow.Control();
    }
    else
    {
        Offroad.Control();
    }
    DcMotor::ControlAllMotors();
    if (!MotorSafety.AfterControl())
    {
        System.Stop(true);
    }
    if (LineFollowBoard::Enabled)
    {
        LineFollow.UpdateDisplay();
    }
}

void RobotSystemCpp(void)
{
    uint32_t wake = osKernelSysTick();
    for (;;)
    {
        FrameTickCpp();
        osDelayUntil(&wake, FRAME_CONTROL_PERIOD_MS);
    }
}
