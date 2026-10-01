#pragma once
#include <stdint.h>
#define SINGLETON(x)                                                                               \
public:                                                                                            \
    static x &GetInstance()                                                                        \
    {                                                                                              \
        static x instance;                                                                         \
        return instance;                                                                           \
    }                                                                                              \
                                                                                                   \
private:                                                                                           \
    x(const x &) = delete;                                                                         \
    x &operator=(const x &) = delete;                                                              \
    x()
/* F103 port uses name lookup, without RTTI/dynamic_cast or heap allocation. */
#define APPLICATION_OVERRIDE                                                                       \
protected:                                                                                         \
    void Start() override;                                                                         \
    void Update() override;
