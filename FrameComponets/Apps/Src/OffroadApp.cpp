#include "OffroadApp.hpp"
#include "stm32f1xx_hal.h"

bool OffroadApp::WatchPoint()
{
    uint32_t now = HAL_GetTick();
    return left_.IsReady() && right_.IsReady() && range_.IsReady() && gray_.IsReady() &&
           range_.HasFreshSample(now) && gray_.HasFreshSample(now);
}

App::Status OffroadApp::GetStatus()
{
    if (System.GetState() == Systems::WORKING && !WatchPoint())
    {
        return App::Error;
    }
    return WatchPoint() ? App::Normal : App::Warning;
}

void OffroadApp::StopMotors()
{
    left_.Stop();
    right_.Stop();
}

void OffroadApp::Start()
{
    StopMotors();
    phase_ = WaitingForHardware;
}

void OffroadApp::SampleSensors()
{
    range_.Update();
    gray_.Update();
}

void OffroadApp::Control()
{
    if (System.GetState() != Systems::WORKING)
    {
        StopMotors();
        return;
    }
    if (!WatchPoint())
    {
        System.Stop(true);
        return;
    }
    /* TODO: gray line error -> steering PID -> differential wheel targets.
     * Then per-wheel PID (if encoders exist) -> signed PWM. Until implemented,
     * remain stopped, even when a start request is made. */
    StopMotors();
}

void OffroadApp::Update()
{
    if (!WatchPoint())
    {
        phase_ = WaitingForHardware;
        return;
    }
    switch (System.GetState())
    {
    case Systems::READY:
        phase_ = WaitingForGesture;
        /* TODO: ultrasonic hand-near/hand-away debounce -> start request.
         * Must only accept a fresh measurement while READY. */
        break;
    case Systems::WORKING:
        phase_ = FollowingLine;
        /* TODO: nonblocking route state graph: white line, 7 dashed groups
         * (50 mm gaps / 100 mm tape), obstacles and final cliff.
         * TODO: distinguish start-line departure from lap crossing; count
         * FRAME_REQUIRED_LAPS (3), then stop after a wheel crosses the black
         * finish line and before touching the obstacle ahead.
         * TODO: noncontact stop event -> System.Stop(). */
        break;
    case Systems::STOP:
    case Systems::ERROR:
        phase_ = Stopped;
        StopMotors();
        break;
    default:
        break;
    }
}
