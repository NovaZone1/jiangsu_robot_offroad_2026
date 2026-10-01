#pragma once

#include "stm32f1xx_hal.h"
#include "SysDefs.hpp"

/// @brief 序列化宏定义，在某个状态函数中实现序列动作
#define SEQLIZE                                                                                    \
    static uint16_t case_id = 0;                                                                   \
    static bool blocked = false;                                                                   \
    static BaseAction *p_act_0, *p_act_1, *p_act_2, *p_act_3;                                      \
    static uint32_t seq_tick = 0;                                                                  \
    switch (case_id)

/// @brief 序列化动作分割符
#define ACTEND                                                                                     \
    blocked ? 0 : case_id++;                                                                       \
    blocked = false;                                                                               \
    break;

/// @brief 序列化结束分割符
#define SEQENDSET(x)                                                                               \
    case_id = 0;                                                                                   \
    x = true;                                                                                      \
    ACTEND

/// @brief 序列化结束循环分割符
#define SEQENDLOOP                                                                                 \
    case_id = 0;                                                                                   \
    ACTEND

#define SEQPARAM &blocked, &seq_tick

class ActionManager; // 前向声明
class BaseAction;
extern ActionManager Action;

class ActionManager
{
public:
    /// @brief 动作状态枚举类型
    typedef enum
    {
        READY, // 准备就绪，尚未开始执行
        RUNNING, // 正在执行中，尚未完成
        COMPLETED, // 已完成，动作已成功执行完毕
        FAILED, // 失败，动作执行过程中发生错误或未达到预期结果
        CANCELED // 已取消，动作被中途取消，未完成
    } ActionStatus;

    void Init();

    /**
     * @brief 持续 追踪/执行 抛出的动作
     * @note 该函数在200Hz的任务中，在系统RUN调用之后被调用
     */
    void ExecutorRun();
    void CancelAll();

    /**
     * @brief 抛出一个即时（同步）动作
     * @note 并不存在真正的即时动作，因为所有动作都需要时间来完成，
     * 唯一的区别是其往往不会持续发出控制指令
     */
    BaseAction *LaunchInstant(BaseAction *hact, uint32_t timeout_ms = 0);

    /**
     * @brief 抛出一个持续（异步）动作
     * @param hact 任务类句柄
     * @param timeout_ms 超时时间，0表示不启用
     */
    BaseAction *LaunchAsync(BaseAction *hact, uint32_t timeout_ms = 0);

    /**
     * @brief 普通的非阻塞式等待
     * @param ms 等待时间，单位毫秒
     */
    void Wait(uint32_t ms, bool *blocked = nullptr, uint32_t *seq_tick = nullptr);

    /**
     * @brief 等待直到
     * @param condition 为1时结束等待
     * @param blocked 阻塞序列用的
     * @param timeout_ms 超时
     */
    void WaitUntil(bool condition, bool *blocked = nullptr, uint32_t *seq_tick = nullptr,
                   uint32_t timeout_ms = 180000);

    BaseAction *RunningActions[12] = {};
    bool RunningActionFlags[12] = {false}; // 哪些动作的槽位被占用
};

class BaseAction
{
private:
    ActionManager::ActionStatus state;
    float process; // 动作完成进度，范围为0.0到1.0，表示动作完成的百分比
    uint32_t timeout_ms; // 超时时间，单位毫秒，0表示不启用
    uint32_t dwt_tick;

public:
    // 构造函数，默认初始化列表
    BaseAction() : state(Action.READY), process(0.0f), timeout_ms(0), dwt_tick(0) {};

    /// @brief 启动动作（通用逻辑，子类无需重写）
    void Start(uint32_t timeout_ms = 0);

    /// @brief 每周期更新（通用逻辑：超时检查 + 调用子类实现的OnUpdate）
    void Update();

    /// @brief 取消动作（通用逻辑 + 子类实现）
    void Cancel();

    /// @brief 获取动作状态（通用接口）
    ActionManager::ActionStatus GetState() const
    {
        return state;
    }

    bool Completed() const
    {
        return state == ActionManager::COMPLETED;
    }

    // 不算完成，但达成了某种条件
    virtual bool Qualified()
    {
        return true;
    }

    // 具体更新逻辑
    virtual bool OnUpdate()
    {
        return true;
    }

    // 子类可以重写，完成、超时、取消的处理函数
    virtual void OnComplete()
    {
    }

    virtual void OnTimeout()
    {
    }

    virtual void OnCancel()
    {
    }
};
