#include "bsp_gpio.h"
#include "frame_config.h"

#define BSPGPIO_MAX_INSTANCES FRAME_MAX_GPIO_INSTANCES
#define BSPGPIO_MAX_EXTICALLBACKS 16 // 最多支持16个外部中断回调函数

/// @brief 记录所有BSP_GPIO实例，便于管理和查找（static化避免外部调用）
static BspGpio_Instance *bspgpio_insts[BSPGPIO_MAX_INSTANCES] = {NULL}; // BSP_GPIO实例注册表
static uint8_t bspgpio_insts_count = 0; // 已注册的BSP_GPIO实例数量

/**
 * @brief 注册BSP_GPIO实例
 * @param inst 指向要注册的BSP_GPIO实例的指针
 * @param port GPIO端口
 * @param pin GPIO引脚
 * @param exti_callback 外部中断回调函数
 */
uint8_t BspGpio_InstRegister(BspGpio_Instance *inst, GPIO_TypeDef *port, uint16_t pin,
                             BspGpio_ExtiCallback exti_callback)
{
    // 检验参数有效性
    if (inst == NULL || port == NULL || pin == 0 || (pin & (pin - 1U)) != 0)
    {
        return 0;
    }

    for (uint8_t i = 0; i < bspgpio_insts_count; ++i)
    {
        BspGpio_Instance *other = bspgpio_insts[i];
        if (other == inst)
        {
            return other->port == port && other->pin == pin;
        }
        if (other->port == port && other->pin == pin)
        {
            return 0;
        }
        if (exti_callback && other->exti_callback && other->pin == pin)
        {
            return 0;
        }
    }
    if (bspgpio_insts_count >= BSPGPIO_MAX_INSTANCES)
    {
        return 0;
    }

    // 初始化实例
    inst->port = port; // GPIO端口
    inst->pin = pin; // GPIO引脚
    inst->exti_callback = exti_callback; // 外部中断回调函数

    // 参数、容量和重复注册检查已通过。
    bspgpio_insts[bspgpio_insts_count++] = inst;
    return 1;
}

/**
 * @brief 根据引脚获取引脚索引
 * @param pin GPIO引脚
 * @return 引脚索引（0-15），无效引脚返回16
 */
static uint8_t BspGpio_GetPinIndex(uint16_t pin)
{
    // 检查是否有且仅传入一个引脚,原则上通过NVIC后在PR寄存器中只能有一个位被置位
    if (pin == 0 ||
        (pin & (pin - 1)) !=
            0) // 因为寄存器中GPIO_Pin的值都是以位掩码存在的，如0x0001,0x0002,0x0004,0x0008...
    {
        return 16; // 无效索引
    }

    // 计算引脚索引
    uint8_t index = 0;
    uint16_t temp_pin = pin;

    // 右移直到最低位为1
    while (temp_pin > 1)
    {
        temp_pin >>= 1;
        index++;
    }
    return index;
}

/**
 * @brief 外部中断回调函数管理器
 * @param pin 触发中断的GPIO引脚
 * @note 由于不同端口的同一引脚号共享一个中断向量，因此将引脚号相同的中断统一管理，后续再进行区分
 */
void BspGpio_ExtiCallbackManager(uint16_t pin)
{
    // 获取引脚索引
    uint8_t pin_index = BspGpio_GetPinIndex(pin);

    // 检查引脚索引有效性
    if (pin_index >= BSPGPIO_MAX_EXTICALLBACKS)
    {
        return; // 无效引脚索引，直接返回
    }

    // 遍历所有注册的BSP_GPIO实例，查找匹配的引脚，并调用对应的外部中断回调函数
    for (uint8_t i = 0; i < bspgpio_insts_count; i++)
    {
        // 获取当前实例
        BspGpio_Instance *inst = bspgpio_insts[i];

        // 检查引脚是否匹配
        if (inst->pin == pin && inst->exti_callback != NULL)
        {
            inst->exti_callback(pin);
            return; // 处理完成，退出函数
        }
    }
}

/**
 * @brief GPIO外部中断回调函数
 * @param GPIO_Pin 触发中断的GPIO引脚
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    // 调用外部中断回调函数管理器
    BspGpio_ExtiCallbackManager(GPIO_Pin);
}

/******      以下是对HAL库函数的复写，供上层使用的接口函数      ******/

/**
 * @brief 获取GPIO引脚的状态
 * @param inst GPIO实例指针
 * @return GPIO引脚状态
 */
GPIO_PinState BspGpio_GetState(BspGpio_Instance *inst)
{
    // 检查参数有效性
    if (inst == NULL || inst->port == NULL)
    {
        return GPIO_PIN_RESET; // 参数无效
    }

    // 调用HAL库函数获取GPIO状态
    return HAL_GPIO_ReadPin(inst->port, inst->pin);
}

/**
 * @brief 设置GPIO引脚的状态
 * @param inst GPIO实例指针
 * @param state GPIO引脚状态
 */
void BspGpio_SetState(BspGpio_Instance *inst, GPIO_PinState state)
{
    // 检查参数有效性
    if (inst == NULL || inst->port == NULL)
    {
        return; // 参数无效
    }

    // 调用HAL库函数设置GPIO状态
    HAL_GPIO_WritePin(inst->port, inst->pin, state);
}

/**
 * @brief 切换GPIO引脚的状态
 * @param inst GPIO实例指针
 */
void BspGpio_ToggleState(BspGpio_Instance *inst)
{
    // 检查参数有效性
    if (inst == NULL || inst->port == NULL)
    {
        return; // 参数无效
    }

    // 调用HAL库函数切换GPIO状态
    HAL_GPIO_TogglePin(inst->port, inst->pin);
}

/**
 * @brief 锁定GPIO引脚
 * @param inst GPIO实例指针
 */
void BspGpio_Lock(BspGpio_Instance *inst)
{
    // 检查参数有效性
    if (inst == NULL || inst->port == NULL)
    {
        return; // 参数无效
    }

    // 调用HAL库函数锁定GPIO引脚
    HAL_GPIO_LockPin(inst->port, inst->pin);
}
