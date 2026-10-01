#pragma once
#include "ultrasonic.hpp"

/* Adapter for the single factory-installed sensor. All public calls belong
 * to the framework task; ReadDistanceCm never triggers or waits for a ping. */
namespace FactoryUltrasonic
{
    enum class Status
    {
        NotInitialized,
        Waiting,
        Ok,
        Timeout,
        InvalidEcho,
        HardwareError,
        Stale
    };

    // Call once in MainFrame.cpp's BindHardware, after System.Init enables DWT.
    // Failure leaves the supplied sensor unbound by this adapter.
    bool Init(Ultrasonic &sensor);

    // Ok: new or cached fresh distance in cm. Any other status: cm = -1.
    // Existing Ultrasonic::Sample stays in millimetres for compatibility.
    Status ReadDistanceCm(float &cm);
} // namespace FactoryUltrasonic
