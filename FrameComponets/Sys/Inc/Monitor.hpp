#pragma once
#include "SysDefs.hpp"
#include <stddef.h>
#include <stdarg.h>

/* Lightweight V1_main monitor. The board supplies a bounded log sink; null
 * sink means disabled logging. Called from the framework thread, never ISR. */
class Monitor
{
    SINGLETON(Monitor) {};

public:
    using Sink = void (*)(const char *, size_t);

    void Init(Sink sink = nullptr)
    {
        sink_ = sink;
    }

    void Run()
    {
    }

    void LogInfo(const char *format, ...);
    void LogOK(const char *format, ...);
    void LogSpec(const char *format, ...);
    void LogRespond(const char *format, ...);
    void LogWarning(const char *format, ...);
    void LogError(const char *format, ...);

private:
    Sink sink_ = nullptr;
    void Write(const char *level, const char *format, va_list args);
};
