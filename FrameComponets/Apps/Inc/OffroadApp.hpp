#pragma once
#include "System.hpp"
#include "dc_motor.hpp"
#include "ultrasonic.hpp"
#include "gray_sensor.hpp"

/* Competition application extension point. This is not an autonomous race
 * implementation; hardware adapters and route strategies are still TODO. */
class OffroadApp : public Application
{
public:
    enum Phase
    {
        WaitingForHardware,
        WaitingForGesture,
        FollowingLine,
        CrossingDashedLine,
        CrossingObstacle,
        Finishing,
        Stopped
    };

    OffroadApp(DcMotor &left, DcMotor &right, Ultrasonic &range, GraySensor &gray)
        : Application("Offroad"), left_(left), right_(right), range_(range), gray_(gray)
    {
    }

    bool WatchPoint() override;
    App::Status GetStatus() override;
    void Control();
    void SampleSensors();
    void StopMotors();

    Phase GetPhase() const
    {
        return phase_;
    }

protected:
    void Start() override;
    void Update() override;

private:
    DcMotor &left_, &right_;
    Ultrasonic &range_;
    GraySensor &gray_;
    Phase phase_ = WaitingForHardware;
};
