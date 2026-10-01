# Yahboom 8-LP：八路 GPIO 灰度驱动

> 更新：用户已确认下文参考引脚，并反馈 OLED 测试检测无误；临时显示入口现已撤下。
> 记录见 [灰度 OLED 测试](../../docs/gray_oled_test.md)。下文为初次驱动交付记录。
> 可复用驱动与主机测试保留；尚未正式绑定比赛 GrayArray，原公共接口保持不变。

## 交付边界

面向当前 STM32F103ZET6 / STM32 HAL / ARM Compiler V6 工程，使用 C++11，复用 C GPIO BSP。
用户已选择八路 GPIO 数字输入。本实现没有 UART、I2C 或 ADC 依赖。
型号为 **8-LP**，不采用 8-GS 的接口假设。

- **新增内容**：`Mods/Inc/gray_yahboom_8lp.hpp`、`Mods/Src/gray_yahboom_8lp.cpp`，
  `Apps/Inc/gray_8lp_example.hpp`、`Apps/Src/gray_8lp_example.cpp`，独立主机测试及本文。
- **修改内容**：未修改任何既有文件；未运行会修改 Keil 工程的同步脚本，未创建提交。
- **可能受影响内容**：未来接入后的灰度自检、新鲜度判断、GPIO 资源、黑白线策略及调度负载。
  新文件目前尚未加入 Keil 工程，也尚未绑定实际硬件，因此不会改变当前固件行为。

## 仓库审查与可复用约定

本节针对当前活动工程；`Resource/other_code` 是参考代码，不作为本次替换目标。

| 目录/层 | 职责、依赖及扩展约定 |
| --- | --- |
| `Core`、根目录 `.ioc` | CubeMX 生成的入口、时钟、GPIO、FreeRTOS、异常/中断配置；共享配置需统一维护 |
| `Drivers` | STM32F1 HAL、CMSIS 与 DSP；复用仓库内版本，不增加依赖或替换第三方库 |
| `Middlewares` | FreeRTOS 10.3.1、CMSIS-RTOS 包装；当前编译 GCC/ARM_CM3 移植层，V6 编译器不等于 RVDS 移植层 |
| `FrameComponets/Bsps` | GPIO/UART/PWM/ADC/编码器/DWT 的 C 接口；处理外设，不放比赛策略 |
| `FrameComponets/Mods` | 设备及传感器接口、硬件适配、数据有效性；依赖 BSP，硬件句柄由 Apps 提供 |
| `FrameComponets/Algorithm` | PID、ADRC、滤波、矩阵等算法；Cortex-M3 软件浮点，不硬编码板级引脚 |
| `FrameComponets/Sys` | 应用生命周期、System 状态、自检、Action、StateCore、日志和 RTOS 桥接 |
| `FrameComponets/Apps` | MainFrame 板级绑定、Offroad 比赛流程与循迹解释；调度桥接 RtosCpp 当前直接引用 MainFrame |
| `MDK-ARM` | Keil 工程及启动文件；新增 `.cpp` 不会自动参加现有工程构建 |
| `tests/frame`、`tools` | 主机模拟测试、格式检查、工程同步；现有测试脚本显式枚举编译源文件 |

保持 `FrameComponets` 原拼写、各层平铺 `Inc/Src` 目录。使用 4 空格、Allman 大括号、
100 列、UTF-8/LF、私有成员尾部下划线；头文件声明、源文件实现。
正常错误返回 `bool` 或无效样本，不增加异常/动态分配。注册对象终身有效，单任务拥有和访问；
不从 ISR 注册设备或驱动整套应用。HAL GPIO/UART 回调已有 BSP 所有者，不能重定义。

关键公共接口与复用位置：

| 接口 | 约定与本次处理 |
| --- | --- |
| `BspGpio_InstRegister` / `BspGpio_GetState` | 注册容量 48；验证重复引脚，无注销接口。本驱动占用 8 个普通 GPIO 注册项，不使用 EXTI |
| `GraySensor::Bind(context, Reader)` | 绑定现有回调，签名不变；用新增 `GrayYahboom8Lp::Read` 适配 |
| `GraySensor::Sample` | 最多 16 路；`line[]` 白色 1、黑色 0；按车体从左到右排列，实际 `channels=8` |
| `GraySensor::Update` | Reader 返回 false 保留旧样本；返回 true 且 valid=false 使旧样本失效 |
| `HasFreshSample` | 灰度默认最大年龄 50 ms；使用 HAL 毫秒时间基准 |
| `GetLineError` | 现有白线质心，且自身不检查新鲜度；黑线示例另加函数，不改其语义 |
| `Ultrasonic::Reader` | 距离毫米、时间戳、有效性；默认新鲜度 200 ms，保持不动 |
| `DcMotor::Driver` | 有符号占空比 [-1,1]、可靠 Stop、可选输出轮 RPM 回调；本驱动不控制电机 |
| `BindHardware` | 现有集中绑定扩展点，当前为空；必须在 GPIO 初始化后注册模块 |
| `OffroadApp::SampleSensors/Control/Update` | 分开采样、控制和流程；当前 Control 始终停车，实际循迹策略尚未完成 |
| `System` / `StateCore` / `Action` / `Monitor` | 保持注册、自检、失效停车、状态图、非阻塞动作和有界日志机制 |

配置仍集中在 `Bsps/Inc/frame_config.h`、`.ioc`、`Core/Inc/FreeRTOSConfig.h` 和 Keil 工程。
前者保持容量与 1 ms 控制 / 5 ms 系统周期；驱动自身新增 `Config` 仅管理采样及防抖。
构建仍由 Keil V6 Rebuild 完成；依赖管理没有引入包管理器。
原测试入口仍为 `tests/frame/Run-Tests.ps1`。新增独立入口是为了在未获既有文件修改确认前验证模块；
正式集成后再经确认更新原测试源列表。

审查发现的约束及风险：

1. 活动工程没有可直接替换的四路灰度硬件实现，只有多通道 GraySensor 接口；不能声称旧四路算法已无缝迁移。
2. `Core/Inc/FreeRTOSConfig.h` 将 `INCLUDE_vTaskDelayUntil` 设为 0，
   `Sys/Src/RtosCpp.cpp` 却调用 `osDelayUntil`；当前 CMSIS 包装在此配置下返回错误，不能实现预期等待。
   1 ms/5 ms 是设计目标，尚不能作为实际周期保证。本驱动按 HAL tick 限速，不能修复系统忙循环。
   此既有问题本次未改，整车联调前需单独确认最小修复并测量调度。
3. TIM6 已用于 HAL 时间基准；GPIO 分配还需避开电机、编码器、超声波和调试接口。
4. BSP 注册无注销/事务回滚；驱动 Init 若中途注册失败，已登记部分仍占用资源。
   必须保留对象生命期、停止接入，修正分配后重启；不能销毁对象、反复重试或复制该对象。
5. CubeMX 重新生成可能影响 V6/FreeRTOS 移植层、用户初始化调用、512 words 栈和 8192 bytes 堆。
6. GPIO 没有帧序号、校准完成信号或通信超时，无法自动识别传感器掉电、卡死、悬空、误入校准。
   有效样本说明本机读到了电平，不证明模块内部刚完成一次光学采样。

## 硬件原理与接口

8-LP 由八个红外反射探头及板载 MCU 处理黑白反差。GPIO 输出模块内部阈值判定后的八路电平，
每路原始返回值仅为 **0 或 1**，不含连续模拟灰度。
供电按厂商资料使用 **5 V，并与主控共地**；5 V 电源不能等同于 GPIO 高电平电压。
厂商产品问答说明通信逻辑为 3.3 V，但问题针对 UART/I2C；上板前仍需核对当前版本 x1～x8 的
高电平/输出结构与所选 STM32 管脚耐压，不能假定所有 F103 引脚均耐 5 V。
如实际输出超过所选引脚允许值，应匹配电平转换后再连接。
来源：[产品介绍与厂商电平答复](https://category.yahboom.net/products/8-lp)。

下表来自[官方 STM32 GPIO 接线示例](https://www.yahboom.net/public/upload/upload-html/1731554674/STM32-IO.html)，
**只是可评审的参考接线，不是本车已确认的资源分配，也未写入活动固件**。

| 8-LP 信号 | 官方示例 STM32 引脚 | 读取方式 | 驱动原始值 |
| --- | --- | --- | --- |
| x1 | PC0 | GPIO 输入 | 0/1 |
| x2 | PC1 | GPIO 输入 | 0/1 |
| x3 | PC2 | GPIO 输入 | 0/1 |
| x4 | PC3 | GPIO 输入 | 0/1 |
| x5 | PA4 | GPIO 输入 | 0/1 |
| x6 | PA5 | GPIO 输入 | 0/1 |
| x7 | PB0 | GPIO 输入 | 0/1 |
| x8 | PB1 | GPIO 输入 | 0/1 |
| VCC / GND | 合适的 5 V 电源 / 共地 | 供电 | 不参与采样 |

实际接线以板上丝印为准，不能用排线颜色猜测针序。八路 GPIO 加电源和地共需 10 根连接线；
不连接串口或 I2C 也可以按此方式读取。
`Channel[0..7]` 必须按车体左到右排列：若安装后 x8 在左，反转配置数组顺序。
`black_level` 为逐通道显式参数。示例用黑低电平，但须在校准后的实物上逐路验证黑/白电平。

驱动默认最小采样间隔 1 ms，范围 1～50 ms；默认连续 2 次相同结果确认状态，范围 1～8 次。
实际调用慢于设定周期时不补采历史数据；该周期是 MCU 的读取间隔，不是厂商保证的内部扫描频率。
官方演示的 300 ms 打印间隔不能用作传感器响应时间。
八个 GPIO 依次读取后整体发布；不会暴露半更新数组，但不具备硬件同时锁存能力。
快速经过边缘时仍应实测通道间时差与总延迟。

## 数据与调用契约

| 数据/接口 | 含义 |
| --- | --- |
| `Frame::raw[8]` | 未经防抖的电气电平 0/1，逻辑左到右 |
| `raw_high_mask` | bit0 是逻辑最左；1 表示高电平；范围 0～255 |
| `black_mask` | 同样 bit0 最左；1 表示防抖后的黑色；范围 0～255，仅 valid 时可用于判断 |
| `timestamp_ms` | 本机本次八路读取完成时间，HAL_GetTick 基准，不是模块 ADC 时间 |
| `valid` | 初始化成功、预热完成、操作者确认校准、全部通道首次防抖完成 |
| `Init` | 校验配置并注册八路 BSP，不修改时钟/引脚模式；失败返回 false |
| `Poll` | 到期读八路并返回 true；未初始化或未到期返回 false，不等待 |
| `Read` | GraySensor 回调；新帧无效也返回 true，及时作废旧结果 |
| `ConfirmCalibration` | 校准前 false；人工确认板上成功且黑白检查正确后 true；重置防抖 |
| `NotifyPowerOn` | 已知模块独立重启/重新上电时调用，重新开始 20 秒预热并撤销校准确认 |
| `IsReady` | 初始化、预热与校准确认条件；不代表初次防抖已完成或线路自检通过 |

进入校准/掉电处理必须在同一框架任务内执行，并在控制前调用 GraySensor::Update 作废上层旧帧。
不能在中断或另一个任务并发调用上述接口。板子提前上电时仍从 Init 起保守等待 20 秒。
只绑定 Reader 时，由 Reader 唯一调用 Poll；不要另处抢先 Poll 消费采样时机。

## 初始化与绑定示例（未自动接入）

以下为参考引脚对应的完整板级准备示例。实车确认资源后，由 CubeMX 生成同等配置，
不要既执行此函数又在别处重复改 GPIO。

```cpp
#include "gray_yahboom_8lp.hpp"
#include "gray_8lp_example.hpp"

static GrayYahboom8Lp Gray8Lp; // BSP 保留内部对象地址，必须长期存在

static void PrepareReferenceInputs()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {};
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL; // 正常由模块驱动；不能借此检测断线
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static bool BindReferenceGray(GraySensor &array)
{
    // 仅当实际安装 x1 在车体最左、且黑色为低电平时适用。
    const GrayYahboom8Lp::Channel channels[8] = {
        {GPIOC, GPIO_PIN_0, GPIO_PIN_RESET},
        {GPIOC, GPIO_PIN_1, GPIO_PIN_RESET},
        {GPIOC, GPIO_PIN_2, GPIO_PIN_RESET},
        {GPIOC, GPIO_PIN_3, GPIO_PIN_RESET},
        {GPIOA, GPIO_PIN_4, GPIO_PIN_RESET},
        {GPIOA, GPIO_PIN_5, GPIO_PIN_RESET},
        {GPIOB, GPIO_PIN_0, GPIO_PIN_RESET},
        {GPIOB, GPIO_PIN_1, GPIO_PIN_RESET}
    };
    GrayYahboom8Lp::Config config;
    if (!Gray8Lp.Init(channels, config))
    {
        return false; // 保持未绑定、自检不通过
    }
    array.Bind(&Gray8Lp, GrayYahboom8Lp::Read);
    return true;
}
```

HAL 和时钟初始化后准备 GPIO，再在 `BindHardware()` 中对现有 `GrayArray` 绑定。
可直接使用 CubeMX 的 MX_GPIO_Init 代替示例 PrepareReferenceInputs。
绑定成功不自动确认校准；应在实际完成下文校准/检查后，由板级操作入口调用
`Gray8Lp.ConfirmCalibration(true)`，已保存校准也须确认仍适合当前场地。
不要在初始化里无条件置 true；无需 `HAL_Delay(20000)`，等待期继续运行系统但提供无效样本。

现有 `Offroad.SampleSensors()` 已执行 GrayArray.Update，无需再增加一条采样循环。
未来控制示例：

```cpp
const Gray8LpExample::Result line =
    Gray8LpExample::EvaluateBlackLine(GrayArray.GetSample(), HAL_GetTick());
// 仅 Left / Center / Right 时使用 line.error；负值线在左，正值线在右。
// Invalid / NoLine / Ambiguous 交由 Apps 的停车、丢线或路口策略处理。
// 这里不直接发电机指令，也不替代现有系统自检。
```

## 黑线位置与四路兼容

采用等间距权重 `{-7,-5,-3,-1,1,3,5,7}`，黑色为贡献项，
`error = 黑色权重和 / (7 * 黑色通道数)`，范围 [-1,1]。
若探头实际间距不均，应另增真实位置权重配置，不能把归一化结果直接当毫米。
本例把任一中央通道或中央两路作为 Center；仍保留实际误差供上层控制使用。

| black_mask | 解释 | 示例结果 |
| --- | --- | --- |
| 0x01 | 仅逻辑左一为黑 | Left，-1 |
| 0x18 | 中央两路为黑 | Center，0 |
| 0x80 | 仅逻辑右一为黑 | Right，+1 |
| 0x00 | 未检测到黑线 | NoLine，不直接断定已出赛道 |
| 0xFF | 全黑 | Ambiguous，可能是宽黑区/横线 |
| 0x81 | 左右两段黑线 | Ambiguous，需比赛流程上下文 |

`error` 在后三类及 Invalid 时不可用于转向；代码中的默认 0 不是“居中”的证据。
逐帧误差向负方向发展表示线位置趋向左，向正方向发展表示趋向右；转向电机符号仍需实车验证。
样本超过 50 ms、通道数不为 8、valid=false 或存在非法值时返回 Invalid。

GraySensor 接口本来支持 16 路，所以公共结构、回调、返回值语义无需改变；只令 channels=8。
旧调用方若写死 4 次循环、4 位掩码、固定 4 元素数组或旧权重，不能直接继续使用。
本次未找到活动四路实现，无法替其验证这些假设。
`Gray8LpExample::ToFourZones(black_mask)` 将相邻 2 路 OR 成一组，返回 0～15；
bit0 为最左组，1 代表组内出现黑色。这是明确有损的兼容示例，既不等于旧模块原始电平，
也不保证旧四探头安装位置一致。必须先检查八路帧有效性再转换。
框架比赛主策略仍是白线；使用白线时保留 GraySensor::GetLineError 并先检查新鲜度。

## 阈值校准与替换检查

按[官方使用说明](https://www.yahboom.net/public/upload/upload-html/1731564712/8-channel%20line%20tracking%20module%20usage.html)：
每次上电等待至少 20 秒。首次红灯闪烁表示未校准；长按 KEY1 至红灯常亮，
在实际安装高度将各探头置于黑色样本，静止 3 秒后短按 KEY1；换白色样本，
再静止 3 秒后短按。红灯熄灭成功，慢闪失败需重做。校准保存到模块，场地或高度改变需重做。

GPIO 模式不能读取/写入数值阈值；本机没有可供设定“ADC 阈值 2048”的输入。
应使用能覆盖全部探头的黑白材料完成逐路黑白检查，确认八路输出均能翻转。
`debounce_samples` 只调整时间防抖，不能弥补黑白材料反差不足，也不是光学阈值。
黑线漏检/边缘抖动时先检查高度、照明、污渍、线材反光与校准，再按车速调整防抖。
需要数值阈值、反射强度或模拟加权时，必须另选模拟数据可达的接口，本次 GPIO 方案不能提供。

相比未明确型号的旧四路模块，仅能确认通道由 4 增至 8、信号线和 GPIO 资源增加；
不能假定旧模块电压、黑白极性、插头针序或孔距与 8-LP 相同。
把阵列中央两探头之间对齐车体中心，保持左右高度一致；前伸距离、阵列宽度、探头间距改变后，
重新验证弯道提前量、窄线覆盖、全黑识别和 PID 参数，旧四路调参不应直接视为有效。
电源应避开电机噪声影响，模块/主控共地；实际高度、遮光和车速条件下测量读数稳定性。

## 验证与待确认的最小接入方案

已运行原主机回归与新增 GPIO 测试：

```powershell
.\tests\frame\Run-Tests.ps1
.\tests\frame\Run-Gray8LpTests.ps1
```

新测试编译真实 bsp_gpio、GraySensor、新驱动和黑线示例，仅模拟 HAL GPIO/时间。
覆盖非法配置/重复注册、注册中途失败、八路顺序/极性、防抖、采样限速、20 秒预热、
校准期间旧帧失效、模块重启、毫秒回绕、新鲜度边界、非法数据及全部 256 种掩码。
使用 C++11、-Wall -Wextra -Werror，无额外依赖。原测试输出位置和入口均未改动。
新 C/C++ 文件按当前可用 clang-format 22.1.3 排版检查；仓库要求 23+，因此不宣称已满足其完整版本要求。
尚未完成加入新源文件后的 Keil V6 全量链接和实板测试，不能把主机通过等同于硬件完成。

经用户确认引脚和授权修改既有文件后，最小方案如下；以下均尚未执行：

| 既有文件 | 修改原因/最小动作 | 影响与兼容性风险 |
| --- | --- | --- |
| `AGENT.md` 资源表 | 只补 8-LP、八路 GPIO、选定引脚、极性和方向 | 避免多人分配冲突，不改旧约定 |
| 根目录 `.ioc`、`Core/Src/gpio.c`，必要时 `Core/Inc/main.h` | 增加八个输入及对应时钟/标签 | 引脚复用、GPIO 模式与生成器覆盖风险；生成后逐项核对 |
| `Apps/Src/MainFrame.cpp` | 静态驱动对象、Init 失败处理、GrayArray.Bind、明确校准确认入口 | 自检增加预热/确认等待；不改旧公共 API |
| `MDK-ARM/jiangsu_robot_offroad_2026.uvprojx` | 用原同步机制加入两个新 cpp，不更换编译器/依赖 | 审查同步差异，确保唯一 FreeRTOS port.c 和原选项 |
| `tests/frame/Run-Tests.ps1` | 接入独立新增测试入口，保留原测试 | 避免默认回归遗漏驱动；需相同 g++ 环境 |

实时调度问题另行提出：最小候选是启用现有 vTaskDelayUntil 支持，核对生成配置并测试
osDelayUntil 的返回及实际周期。该动作属于既有配置变更，必须先确认，未混入驱动实现。
循迹电机策略是另外的应用开发，不在本次驱动接入中自动开启。

重点硬件回归：逐路黑白/左右顺序，所有电平组合中的典型左中右/全白/全黑/交叉，
上电与校准期间保持未就绪，真实模块掉电/断线的系统行为，50 ms 过期停车，
电机及超声波并行资源冲突，实际 1 ms/5 ms 周期、任务栈余量，CubeMX 再生成和 V6 全量 Rebuild。
