#pragma once
#include "OffroadApp.hpp"
#include "led.hpp"
extern DcMotor LeftMotor, RightMotor;
extern Ultrasonic RangeSensor;
extern GraySensor GrayArray;
extern OffroadApp Offroad;
extern Led StatusLed;
void MainFrameCpp();
