#pragma once

struct OledTestMonitor
{
    unsigned warnings = 0;
    void LogWarning(const char *)
    {
        ++warnings;
    }
};
struct OledTestSystem
{
    OledTestMonitor monitor;
};
extern OledTestSystem System;
