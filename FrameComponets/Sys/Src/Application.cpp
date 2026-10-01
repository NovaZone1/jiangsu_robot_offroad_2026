#include "Application.hpp"

#include <string.h>

Application::Application(const char *name)
{
    strncpy(name_, name ? name : "", sizeof(name_) - 1U);
    name_[sizeof(name_) - 1U] = '\0';
}

const char *Application::GetName() const
{
    return name_;
}

bool Application::CntFull()
{
    const uint8_t period = prescaler ? prescaler : 1U;
    if (++counter_ < period)
    {
        return false;
    }

    counter_ = 0;
    return true;
}

bool Application::WatchPoint()
{
    return true;
}

App::Status Application::GetStatus()
{
    return App::Normal;
}
