#pragma once
#include "OffroadApp.hpp"
#include "led.hpp"
#include "motor_pwm_driver.hpp"
extern MotorPwmDriver Motor1Driver, Motor2Driver, Motor3Driver, Motor4Driver;
extern DcMotor Motor1, Motor2, Motor3, Motor4;
extern DcMotor &LeftFrontMotor, &LeftRearMotor, &RightFrontMotor, &RightRearMotor;
// 保留旧比赛应用名称，分别引用左前 M1 和右前 M3。
extern MotorPwmDriver &LeftMotorDriver, &RightMotorDriver;
extern DcMotor &LeftMotor, &RightMotor;
extern Ultrasonic RangeSensor;
extern GraySensor GrayArray;
extern OffroadApp Offroad;
extern Led StatusLed;
void MainFrameCpp();
