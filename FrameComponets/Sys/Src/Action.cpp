#include "Action.hpp"
#include "bsp_dwt.h"

// 全局唯一的动作管理器实例
ActionManager Action;

void ActionManager::CancelAll()
{
    for (int i = 0; i < 12; ++i)
    {
        if (RunningActionFlags[i] && RunningActions[i])
        {
            RunningActions[i]->Cancel();
        }
        RunningActions[i] = nullptr;
        RunningActionFlags[i] = false;
    }
}

/**
 * @brief 启动动作（通用逻辑，子类无需重写）
 * @param timeout_ms 超时时间，单位毫秒，0表示不启用
 * @note 该函数会将动作状态设置为RUNNING，记录超时时间和当前时间戳，并重置完成进度。
 */
void BaseAction::Start(uint32_t timeout_ms)
{
    state = Action.RUNNING; // 将状态设置为RUNNING
    this->timeout_ms = timeout_ms;
    dwt_tick = HAL_GetTick();
    process = 0.0f;
}

/**
 * @brief 更新动作状态（通用逻辑，子类无需重写）
 * @note 该函数会检查动作是否超时，并调用子类的OnUpdate方法来更新完成进度。
 * 如果OnUpdate返回true，则将状态设置为COMPLETED并调用OnComplete方法。
 */
void BaseAction::Update()
{
    // 若状态不是RUNNING，则不进行更新
    if (state != Action.RUNNING)
    {
        return;
    }

    // 超时检查（通用逻辑）
    if (timeout_ms > 0 && (uint32_t)(HAL_GetTick() - dwt_tick) >= timeout_ms)
    {
        state = Action.FAILED; // 判定任务失败（超时）
        OnTimeout(); // 调用子类的超时处理函数
        return; // 超时后不再执行OnUpdate
    }

    if (OnUpdate())
    {
        state = Action.COMPLETED; // 判定任务完成
        OnComplete(); // 调用子类的完成处理函数
    }
}

/**
 * @brief 取消动作（通用逻辑 + 子类实现）
 * @note 该函数会将动作状态设置为CANCELED，并调用子类的OnCancel方法来执行取消后的处理逻辑。
 */
void BaseAction::Cancel()
{
    if (state == Action.RUNNING)
    {
        state = Action.CANCELED; // 判定任务已取消
        OnCancel(); // 调用子类的取消处理函数
    }
}

/**
 * @brief 启动动作管理器
 * @note 该函数可以用于初始化动作管理器的状态或资源。
 */
void ActionManager::Init()
{
    // 清空所有动作槽位，确保初始状态一致
    for (int i = 0; i < 12; i++)
    {
        RunningActions[i] = nullptr;
        RunningActionFlags[i] = false;
    }
}

/**
 * @brief 持续 追踪/执行 抛出的动作
 * @note
 * 该函数会遍历当前正在执行的动作列表，调用每个动作的Update方法来更新它们的状态。如果某个动作已完成、失败或取消，则将其从列表中移除。
 */
void ActionManager::ExecutorRun()
{
    for (int i = 0; i < 12; i++)
    {
        // 如果该槽位有动作
        if (RunningActionFlags[i])
        {
            BaseAction *action = RunningActions[i];

            // 每周期更新动作状态
            action->Update();

            // 检查动作是否结束（完成、失败或取消）
            if (action->GetState() == Action.COMPLETED || action->GetState() == Action.FAILED ||
                action->GetState() == Action.CANCELED)
            {
                // 从列表中移除动作
                RunningActionFlags[i] = false;
                RunningActions[i] = nullptr;
            }
        }
    }
}

/**
 * @brief 抛出一个即时（同步）动作
 * @note
 * 该函数会将动作添加到当前正在执行的动作列表中，并调用动作的Start方法来启动它。由于是即时动作，通常不会持续发出控制指令，因此不需要在ExecutorRun中持续更新。
 */
BaseAction *ActionManager::LaunchInstant(BaseAction *hact, uint32_t timeout_ms)
{
    for (int i = 0; i < 12; ++i)
    {
        if (RunningActionFlags[i] && RunningActions[i] == hact)
        {
            return nullptr;
        }
    }
    if (hact == nullptr)
    {
        return nullptr; // 无效的动作指针
    }

    // 查找空闲槽位
    for (int i = 0; i < 12; i++)
    {
        if (!RunningActionFlags[i])
        {
            RunningActions[i] = hact;
            RunningActionFlags[i] = true;
            hact->Start(timeout_ms); // 启动动作
            return hact;
        }
    }

    // 无可用槽位，启动失败
    return nullptr;
}

/**
 * @brief 抛出一个持续（异步）动作
 * @note
 * 该函数会将动作添加到当前正在执行的动作列表中，并调用动作的Start方法来启动它。由于是持续动作，需要在ExecutorRun中持续更新它的状态。
 */
BaseAction *ActionManager::LaunchAsync(BaseAction *hact, uint32_t timeout_ms)
{
    for (int i = 0; i < 12; ++i)
    {
        if (RunningActionFlags[i] && RunningActions[i] == hact)
        {
            return nullptr;
        }
    }
    if (hact == nullptr)
    {
        return nullptr; // 无效的动作指针
    }

    // 首先将动作添加到运行列表
    bool slot_found = false;
    for (int i = 0; i < 12; i++)
    {
        if (!RunningActionFlags[i])
        {
            RunningActions[i] = hact; // 将动作添加到列表
            RunningActionFlags[i] = true; // 标记该槽位被占用
            slot_found = true;
            break;
        }
    }

    // 没有找到可用的槽位，无法启动新动作
    if (!slot_found)
    {
        return nullptr;
    }

    // 启动动作
    hact->Start(timeout_ms);

    return hact; // 返回启动的动作指针
}

/**
 * @brief 普通的非阻塞式等待
 * @param ms 等待时间，单位毫秒
 */
void ActionManager::Wait(uint32_t ms, bool *blocked, uint32_t *seq_tick)
{
    if (!blocked || !seq_tick)
    {
        return;
    }
    // 记录起始时间
    if (*seq_tick == 0)
    {
        *seq_tick = HAL_GetTick() + 1U;
    }

    // 检查等待时间是否已到
    if ((uint32_t)(HAL_GetTick() + 1U - *seq_tick) >= ms)
    {
        *seq_tick = 0; // 重置序列计数器
        if (blocked)
        {
            *blocked = false; // 解除阻塞状态
        }
    }
    else
    {
        if (blocked)
        {
            *blocked = true; // 进入阻塞状态
        }
    }
}

/**
 * @brief 等待直到
 * @note
 * 该函数会持续检查条件是否满足，或者是否超时。如果条件满足或超时，则解除阻塞状态；否则保持阻塞状态。
 */
void ActionManager::WaitUntil(bool condition, bool *blocked, uint32_t *seq_tick,
                              uint32_t timeout_ms)
{
    if (!blocked || !seq_tick)
    {
        return;
    }
    // 记录起始时间
    if (*seq_tick == 0)
    {
        *seq_tick = HAL_GetTick() + 1U;
    }

    // 检查条件是否满足
    if (condition)
    {
        *seq_tick = 0; // 重置序列计数器
        if (blocked)
        {
            *blocked = false; // 解除阻塞状态
        }
    }
    // 检查超时
    else if (timeout_ms > 0 && (uint32_t)(HAL_GetTick() + 1U - *seq_tick) >= timeout_ms)
    {
        *seq_tick = 0;
        if (blocked)
        {
            *blocked = false;
        }
    }
    // 保持阻塞
    else
    {
        if (blocked)
        {
            *blocked = true;
        }
    }
}
