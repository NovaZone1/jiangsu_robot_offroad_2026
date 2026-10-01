#pragma once

#include <stdint.h>

namespace App
{
    enum Status : uint8_t
    {
        Normal,
        Warning,
        Error
    };
} // namespace App

// 应用生命周期：注册时 Start，按 prescaler 分频调用 Update。
class Application
{
    friend class RobotSystem;

public:
    uint8_t prescaler = 1;
    App::Status status = App::Normal;

    const char *GetName() const;
    bool CntFull();
    virtual bool WatchPoint();
    virtual App::Status GetStatus();

protected:
    explicit Application(const char *name);
    virtual void Start() = 0;
    virtual void Update() = 0;

private:
    char name_[24];
    uint8_t counter_ = 0;
};
