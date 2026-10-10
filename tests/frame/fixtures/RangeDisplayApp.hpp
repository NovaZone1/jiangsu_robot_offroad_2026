#pragma once
#include "Application.hpp"
#include "oled_factory.hpp"

// Host-only diagnostic display regression; not a production application.
class RangeDisplayApp : public Application
{
public:
    RangeDisplayApp() : Application("RangeDisplay")
    {
    }

    App::Status GetStatus() override;

protected:
    void Start() override;
    void Update() override;

private:
    FactoryOled display_;
    uint32_t last_frame_ms_ = 0;
};

extern RangeDisplayApp RangeDisplay;
