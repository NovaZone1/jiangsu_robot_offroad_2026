#include "StateCore.hpp"
#include "stdio.h"
#include "bsp_dwt.h"
#include "Monitor.hpp"
#include "cmsis_os.h"

StateBlock::StateBlock() : StateBlock("")
{
}

StateBlock::StateBlock(const char *name) : linkNums(0), Complete(false)
{
    strncpy(this->name, name, sizeof(this->name) - 1U);
    this->name[sizeof(this->name) - 1U] = '\0';
}

StateGraph::StateGraph(const char *name) : stateNums(0)
{
    strncpy(this->name, name, sizeof(this->name) - 1U);
    this->name[sizeof(this->name) - 1U] = '\0';
}

/**
 * @brief 为状态块添加状态链接
 */
bool StateBlock::LinkTo(bool *condition, StateBlock &nextState)
{
    if (condition != nullptr && linkNums < 16)
    {
        links[linkNums].condition = condition;
        links[linkNums].nextState = &nextState;
        linkNums++;
        return true;
    }
    return false;
}

/**
 * @brief 进行状态转移，返回下一个状态ID
 * @return 如果没有状态转移则返回当前状态ID，否则返回下一个状态ID
 */
uint8_t StateBlock::Transition()
{
    for (int i = 0; i < linkNums; i++)
    {
        if (*(links[i].condition))
        {
            return links[i].nextState->id;
        }
    }
    return id;
}

/**
 * @brief 向状态图中添加状态块
 */
StateBlock &StateGraph::AddState(const char *name)
{
    if (stateNums < 24)
    {
        // 获取目标状态
        StateBlock &target = states[stateNums];

        // 初始化目标状态
        target = StateBlock(name);
        target.id = stateNums;
        target.Complete = false;

        // 增加状态数量
        stateNums++;
        return target;
    }
    // 如果状态数量已满，返回第一个状态（虽然这可能会导致问题，但这是一个简单的错误处理方式）
    return states[0];
}

/**
 * @brief 设置状态图的全局状态函数
 */
void StateGraph::SetGlobalAct(void (*GlobalAction)(StateCore *core))
{
    this->GlobalAction = GlobalAction;
}

/**
 * @brief 状态机的简并初始化，一般用于调试
 * @details 只有两个状态：working和end
 */
bool StateGraph::Degenerate(void (*DegenAction)(StateCore *core))
{
    if (stateNums != 0 || DegenAction == nullptr)
    {
        return false;
    }
    // 固定两个状态

    // 第一个状态：working
    StateBlock &state_work = AddState("working");
    state_work.StateAction = DegenAction;
    state_work.LinkTo(&(states[0].Complete), states[1]); // 当working状态完成时，转移到end状态

    // 第二个状态：end
    StateBlock &state_end = AddState("end");
    state_end.StateAction = nullptr; // end状态没有执行函数

    return true;
}

/**
 * @brief 运行状态机核心
 */
void StateCore::Run(void)
{
    // 避免空运行
    if (graphNums == 0 || at_graph_id >= graphNums || !enabled_)
    {
        return;
    }

    // 计算时间间隔
    delta_time_ = BspDwt_GetDeltaTime(&last_cycle_);

    // 执行 `当前状态图` 的 `对应状态`的 状态函数
    StateGraph &graph = *graphs[at_graph_id];
    if (graph.executor_at_id >= graph.stateNums)
    {
        return;
    }
    StateBlock &state = graph.states[graph.executor_at_id];

    // 执行当前状态图的全局状态函数
    if (graph.GlobalAction != nullptr)
    {
        graph.GlobalAction(this);
    }
    if (!enabled_)
    {
        return;
    }

    // 状态函数返回后再判断转移；函数内 Disable() 可以终止本轮执行。
    if (state.StateAction != nullptr)
    {
        state.StateAction(this);
    }
    if (!enabled_)
    {
        return;
    }

    // 进行状态转移
    uint8_t next = state.Transition();
    if (next < graph.stateNums)
    {
        graph.executor_at_id = next;
        graph.current_state = &graph.states[next];
    }
}

/**
 * @brief 启动状态机核心
 */
void StateCore::Enable(uint8_t first_graph)
{
    if (first_graph < graphNums && graphs[first_graph]->stateNums != 0)
    {
        at_graph_id = first_graph;
        enabled_ = true;
        last_cycle_ = DWT->CYCCNT;
    }
}

/**
 * @brief 注册状态图
 */
void StateCore::RegistGraph(StateGraph &graph)
{
    for (uint8_t i = 0; i < graphNums; ++i)
    {
        if (graphs[i] == &graph)
        {
            return;
        }
    }
    if (graphNums < 4)
    {
        graphs[graphNums] = &graph;
        graphNums++;
    }
    else
    {
        // 处理状态图数量已满的情况
        Monitor::GetInstance().LogWarning("StateCore: Too much state graph!");
    }
}

/**
 * @brief 获得当前状态的引用
 */
StateBlock &StateCore::GetCurState()
{
    static StateBlock empty("inactive");
    if (!graphNums || at_graph_id >= graphNums)
    {
        return empty;
    }
    StateGraph &graph = *graphs[at_graph_id];
    if (graph.executor_at_id >= graph.stateNums)
    {
        return empty;
    }
    return graph.states[graph.executor_at_id];
}

/**
 * @brief 绘制状态机图
 * @details 通过遍历整个状态机，将状态机的状态和状态转换关系以图的形式发送到指定串口上 'Mermaid'
 * @warning 该函数会阻塞程序运行！！因此禁止在线程中调用，仅做Debug用途
 */
void StateCore::CoreGraph(const StateGraph &graph)
{
    uint8_t buf[60];
    Monitor::GetInstance().LogInfo("StateGraph\n");

    HAL_Delay(10);
    for (int i = 0; i < graph.stateNums; i++)
    {
        // 发送状态转换关系（mermaid格式）
        for (int j = 0; j < graph.states[i].linkNums; j++)
        {
            snprintf((char *)buf, sizeof(buf), "%s --> %s", graph.states[i].name,
                     graph.states[i].links[j].nextState->name);
            Monitor::GetInstance().LogInfo("%s", (char *)buf);
            HAL_Delay(10);
        }
    }
}

namespace Seq
{
    /**
     * @brief 等待指定时间
     * @param sec 等待时间，单位秒
     * @details 利用osDelay实现
     */
    void Wait(float sec)
    {
        osDelay((uint32_t)(sec * 1000)); // 将秒转换为毫秒
    }

    /**
     * @brief 等待直到条件满足或超时
     * @param condition 指向布尔条件的引用
     * @param timeout_sec 超时时间，单位秒，默认3600秒
     * @details 利用阻塞+让步实现
     */
    void WaitUntil(bool &condition, float timeout_sec)
    {
        uint32_t start_tick = xTaskGetTickCount(); // 获取当前系统时间（tick）
        uint32_t timeout_ticks = (uint32_t)(timeout_sec * 1000); // 将超时时间转换为tick

        // 储存当前任务优先级
        osPriority original_priority = osThreadGetPriority(osThreadGetId());
        // 每轮休眠 1 ms，等待期间保持原任务优先级。

        while (!condition)
        {
            // 检查超时
            uint32_t current_tick = xTaskGetTickCount();
            if ((current_tick - start_tick) >= timeout_ticks)
            {
                break;
            }

            // 让出CPU时间，避免死循环占用过多资源
            osDelay(1);
        }

        // 恢复任务优先级
        osThreadSetPriority(osThreadGetId(), original_priority);
    }

    namespace Detail
    {
        // 私有代理函数的实现
        void WaitUntilImpl(CheckFunctionPtr check_func_ptr, void *context, float timeout_sec)
        {
            uint32_t start_tick = xTaskGetTickCount();
            uint32_t timeout_ticks = (uint32_t)(timeout_sec * 1000);

            // 存储当前任务优先级
            osThreadId current_thread_id = osThreadGetId();
            osPriority original_priority = osThreadGetPriority(current_thread_id);
            // 每轮休眠 1 ms，等待期间保持原任务优先级。

            // 使用传入的函数指针和上下文来检查条件
            while (!check_func_ptr(context))
            {
                // 检查超时
                uint32_t current_tick = xTaskGetTickCount();
                if ((current_tick - start_tick) >= timeout_ticks)
                {
                    break;
                }

                osDelay(1);
            }

            // 恢复任务优先级
            osThreadSetPriority(current_thread_id, original_priority);
        }
    } // namespace Detail
} // namespace Seq
