#pragma once
#include "bluetooth.hpp"
#include "nova_protocol.hpp"

#ifndef NOVA_M1_TUNING
#define NOVA_M1_TUNING 1
#endif

// Board integration: channel 1..4 = M1..M4, 5 = line following.
// Explicit bench firmware selection; no competition application may command wheels in this mode.
class NovaLinkApp
{
public:
    static constexpr uint32_t Baud = 115200;
    static constexpr float MaximumTrialRpm = 150.0f;
    static constexpr bool M1TuningEnabled = NOVA_M1_TUNING != 0;
    bool Init();
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
    bool configured_ = false, trial_started_ = false, trial_fault_sent_ = false;
    const char *trial_fault_ = nullptr; // Latched until MCU reset; STOP never clears it.
    uint32_t heartbeat_tick_ = 0, trial_tick_ = 0, watchdog_ms_ = 1000;
    uint32_t tx_errors_ = 0;
    float actual_min_ = -180, actual_max_ = 180;
};

extern NovaLinkApp NovaLink;
