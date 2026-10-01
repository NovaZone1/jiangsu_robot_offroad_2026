#pragma once

#include "SysDefs.hpp"
#include <stdint.h>
#include "string.h"

class StateBlock;
class StateCore;

#define PASTE2(a, b) a##b
#define NEW_STATE(graph, name)                                                                     \
    StateBlock &PASTE2(St_, name) = (graph).AddState(#name);                                       \
    PASTE2(St_, name).StateAction = PASTE2(Robo, name);

/**
 * @brief 状态链接，代表了状态之间的转换关系
 */
typedef struct
{
    bool *condition; // 条件
    StateBlock *nextState; // 下一个状态
} StateLink;

/**
 * @brief 状态块，代表了一个状态
 */
class StateBlock
{
public:
    StateBlock();
    explicit StateBlock(const char *name);

    void (*StateAction)(StateCore *core) = nullptr;
    char name[16]; // 状态名称
    uint8_t id = 0;
    StateLink links[16] = {};
    uint8_t linkNums; // 状态链接数量
    bool Complete; // 状态完成标志

    /// @brief 添加状态链接
    bool LinkTo(bool *condition, StateBlock &nextState);

    /// @brief 状态转换函数
    uint8_t Transition();
};

/**
 * @brief 状态图，包含了一系列状态块
 */
class StateGraph
{
    friend class StateCore;

private:
    void (*GlobalAction)(StateCore *core) = nullptr;

public:
    StateGraph(const StateGraph &) = delete;
    StateGraph &operator=(const StateGraph &) = delete;
    explicit StateGraph(const char *name);

    char name[16]; // 状态图名称
    StateBlock states[24]; // 状态图包含的状态块，最多24个
    uint8_t stateNums; // 状态图包含的状态块数量

    uint8_t executor_at_id = 0; // 当前执行状态块的ID
    StateBlock *current_state = &states[0]; // 指针随状态转移更新，不复制状态块

    /// @brief 添加状态块，返回添加的状态块的引用
    StateBlock &AddState(const char *name);

    /// @brief 设置全局状态函数
    void SetGlobalAct(void (*GlobalAction)(StateCore *core));

    /// @brief 状态机的简并初始化，一般用于调试
    bool Degenerate(void (*DegenAction)(StateCore *core));
};

/**
 * @brief 状态机核心，即状态机本体
 */
class StateCore
{
    SINGLETON(StateCore) {};

private:
    bool enabled_ = false; // 状态机核心是否启用
    uint32_t last_cycle_ = 0;
    float delta_time_ = 0;

public:
    StateGraph *graphs[4] = {};
    uint8_t graphNums = 0; // 状态机核心包含的状态图数量
    uint8_t at_graph_id = 0;

    /// @brief 循环运行状态机核心
    void Run(void);

    /// @brief 启动状态机核心，可指定初始状态图
    void Enable(uint8_t first_graph = 0);

    void Disable()
    {
        enabled_ = false;
    }

    /// @brief 注册状态图
    void RegistGraph(StateGraph &graph);

    /// @brief 获得当前状态的引用
    StateBlock &GetCurState();

    /// @brief 根据状态图输出Mermaid代码
    static void CoreGraph(const StateGraph &graph);
};

namespace Seq
{
    void Wait(float sec);
    void WaitUntil(bool &condition, float timeout_sec = 3600.0f);

    using CheckFunctionPtr = bool (*)(void *);

    namespace Detail
    {
        /**
         * @brief 等待直到（私有代理函数）
         * @param check_func_ptr: 实际检查函数的地址
         * @param context: 传递给检查函数的Lambda闭包
         * @param timeout_sec: 超时时间，单位秒
         * @note 注意：Lambda 对象 'condition' 是在栈上创建的，它的生命周期仅在 WaitUntil 期间有效。
         */
        void WaitUntilImpl(CheckFunctionPtr check_func_ptr, void *context, float timeout_sec);
    } // namespace Detail

    /**
     * @brief 等待直到（表达式重载）
     * @param condition 布尔条件表达式 (Lambda/Functor)
     * @param timeout_sec 超时时间，单位秒
     */
    template <typename ConditionFunc>
    void WaitUntil(ConditionFunc condition, float timeout_sec = 3600.0f)
    {
        // 注意：Lambda 闭包对象会被传递到这里作为 context
        static auto callback_adapter = [](void *context) -> bool
        {
            // 将 void* 转换回原始的 Lambda 指针
            ConditionFunc *lambda_ptr = static_cast<ConditionFunc *>(context);
            // 执行 Lambda 返回结果
            return (*lambda_ptr)();
        };

        // 调用私有代理函数，传递 Lambda 对象的地址作为 context
        Detail::WaitUntilImpl(callback_adapter, &condition, timeout_sec);
    }
} // namespace Seq
