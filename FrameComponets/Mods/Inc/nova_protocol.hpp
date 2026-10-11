#pragma once
#include <stddef.h>
#include <stdint.h>

// Nova main a0de0488e37597d41cea9bde2aa15752a418261c: legacy PID + v2 @JSON.
// All methods run in one task. No heap allocation, MCU dependency or motor writes here.
class NovaProtocol
{
public:
    enum class Command
    {
        Hello,
        Get,
        Configure,
        Heartbeat,
        SetPid,
        SetTarget,
        Stop
    };

    enum Capability
    {
        SetPid = 1,
        SetTarget = 2,
        Stop = 4,
        Watchdog = 8,
        Limits = 16
    };

    struct Gains
    {
        float kp = 0, ki = 0, kd = 0;
    };

    struct Request
    {
        Command command = Command::Get;
        uint8_t channel = 0;
        Gains gains;
        float value = 0, actual_min = 0, actual_max = 0, output_limit = 0, watchdog = 0;
    };

    struct Reply
    {
        Gains gains;
        float target = 0, period = 0;
        bool enabled = false;
        uint8_t capabilities = 0;
        const char *algorithm = "unspecified";
        const char *error = "rejected";
    };

    struct Callbacks
    {
        void *context = nullptr;
        bool (*send)(void *, const uint8_t *, uint16_t) = nullptr;
        // true only after actual application; false sends a negative ACK.
        bool (*execute)(void *, const Request &, Reply &) = nullptr;
    };

    struct Stats
    {
        uint32_t invalid = 0, received = 0, tx_dropped = 0, duplicates = 0;
    };

    bool Init(const Callbacks &callbacks);
    void Feed(const uint8_t *data, size_t size);
    void DiscardUntilNewline(); // Transport loss: never execute a surviving line suffix.
    bool Telemetry(uint8_t channel, uint32_t sequence, double seconds, float setpoint, float actual,
                   float output);
    bool Fault(uint8_t channel, const char *reason);
    const Stats &GetStats() const;
    uint8_t GetLegacyChannel() const;

private:
    struct Cache
    {
        char id[48] = {};
        char response[384] = {};
        uint32_t hash = 0;
    };

    void ProcessLine();
    void ProcessLegacy();
    bool Send(const char *line);
    Callbacks callbacks_;
    Stats stats_;
    char line_[384] = {}, output_[384] = {};
    Cache cache_[4];
    uint16_t length_ = 0;
    uint8_t legacy_channel_ = 0, next_cache_ = 0;
    bool dropping_ = false;
};
