#pragma once
#include "bluetooth.hpp"
#include "nova_protocol.hpp"

#ifndef NOVA_MOTOR_TUNING
#define NOVA_MOTOR_TUNING 0
#endif

#ifndef NOVA_TUNING_PORT
#define NOVA_TUNING_PORT 4
#endif

// Board integration: channel 1..4 = M1..M4, 5 = line following.
// Explicit bench firmware selection; no competition application may command wheels in this mode.
class NovaLinkApp
{
public:
    static constexpr uint32_t Baud = 115200;
    static constexpr float MaximumTrialRpm = 150.0f;
    static constexpr bool MotorTuningEnabled = NOVA_MOTOR_TUNING != 0;
    static constexpr uint8_t TuningPort = NOVA_TUNING_PORT;
    static constexpr uint8_t TuningIndex = TuningPort - 1;
    static_assert(TuningPort >= 1 && TuningPort <= 4, "Invalid Nova tuning motor port");
    bool Init();
    // Local bench owns movement; remote access is query/telemetry plus a latched STOP only.
    bool InitMonitor(uint8_t channel, void (*stop)());
    void Service(); // Same 1 ms framework task; also serviced while motor faults are latched.
    void AfterControl();
    const NovaProtocol &GetProtocol() const;
    const Bluetooth &GetBluetooth() const;

private:
    static bool Send(void *, const uint8_t *, uint16_t);
    static bool Execute(void *, const NovaProtocol::Request &, NovaProtocol::Reply &);
    void Publish(uint8_t channel);
    bool TrialCommand(const NovaProtocol::Request &, NovaProtocol::Reply &);
    void CheckTrial();
    void StopTrial();
    void FailTrial(const char *reason);
    Bluetooth bluetooth_;
    NovaProtocol protocol_;
    uint32_t last_tick_ = 0, last_publish_ = 0, sequences_[5] = {}, samples_[5] = {};
    uint64_t uptime_ms_ = 0;
    uint8_t next_channel_ = 1;
    bool initialized_ = false, have_sample_[5] = {}, fault_sent_[5] = {};
    bool monitor_only_ = false;
    uint8_t monitor_channel_ = 1;
    void (*monitor_stop_)() = nullptr;
    bool configured_ = false, trial_started_ = false, trial_fault_sent_ = false;
    const char *trial_fault_ = nullptr; // Latched until MCU reset; STOP never clears it.
    uint32_t heartbeat_tick_ = 0, trial_tick_ = 0, watchdog_ms_ = 1000;
    uint32_t tx_errors_ = 0;
    float actual_min_ = -180, actual_max_ = 180;
};

extern NovaLinkApp NovaLink;
