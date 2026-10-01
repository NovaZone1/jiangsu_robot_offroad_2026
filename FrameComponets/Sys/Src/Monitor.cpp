#include "Monitor.hpp"

#include <stdio.h>
#include <string.h>

void Monitor::Write(const char *level, const char *format, va_list args)
{
    if (!sink_ || !format)
    {
        return;
    }

    char buffer[128];
    const int prefix = snprintf(buffer, sizeof(buffer), "[%s] ", level);
    if (prefix < 0 || (size_t)prefix >= sizeof(buffer) - 2U)
    {
        return;
    }

    // 为换行和字符串终止符保留空间，截断日志也必须是完整字符串。
    vsnprintf(buffer + prefix, sizeof(buffer) - (size_t)prefix - 1U, format, args);
    size_t size = strlen(buffer);
    buffer[size++] = '\n';
    buffer[size] = '\0';
    sink_(buffer, size);
}

void Monitor::LogInfo(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Write("INFO", format, args);
    va_end(args);
}

void Monitor::LogOK(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Write("OK", format, args);
    va_end(args);
}

void Monitor::LogSpec(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Write("SPEC", format, args);
    va_end(args);
}

void Monitor::LogRespond(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Write("RESP", format, args);
    va_end(args);
}

void Monitor::LogWarning(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Write("WARN", format, args);
    va_end(args);
}

void Monitor::LogError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    Write("ERROR", format, args);
    va_end(args);
}