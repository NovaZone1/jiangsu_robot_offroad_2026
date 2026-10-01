> 当前状态：已按用户要求撤下超声波＋OLED临时测试。MainFrame已恢复原入口，Keil中5个测试相关源文件条目已移除。下文接入步骤仅为历史说明，不代表当前已启用。
> 专用RangeDisplayApp已从FrameComponets移出，仅保留在tests/frame/fixtures供主机回归测试使用。可复用驱动保留，未参与当前固件编译。不要重新应用旧接入补丁；不要用Sync-FrameProject将暂存驱动重新加入工程。用户的Keil版本及RTE设置保留。

# 原厂超声波增量模块

## 交付状态

用户已批准“超声波测距 + OLED 显示”的临时接入，当前 MainFrame 和 Keil 工程已接入。
旧的 `docs/ultrasonic_factory.integration.patch` 仅保留作历史方案，不要再次应用。
移除本次测试请使用 `docs/range_oled_temporary_test.md`，它覆盖统一接入的反向操作。
未修改旧 Ultrasonic 类、Sample、Reader、调用链、Core、RTOS 配置或第三方库。

## 原厂依据与资源占用

工作区相邻的原厂资料提供了以下依据（不依赖网上型号推断）：

- `STM32开发板小车/4、开发板拓展课程/工程源码/8.ultrasonic_show_data/ultrasonic_show_data/ultrasonic_show_data.ioc`：PF11/TRIG、PF12/ECHO、TIM7。
- `STM32开发板小车/6、程序源码汇总/STM32_CAR源码汇总.zip` 内
  `STM32_CAR源码汇总/2.Extended_Course/8.ultrasonic_show_data/ultrasonic_show_data/Core/Inc/main.h`
  和 `BSP/ultrasonic/bsp_ultrasonic.c`：相同引脚、20 us 触发高电平、脉宽 us / 58.5 得到厘米。
- 同一压缩包的 `2.ultrasonic_servo_show_data` 版本以 500 cm 作为上限。

本实现使用单次测量，不照搬原厂阻塞的五次平均、等待 ECHO 的 while 循环及 HAL_Delay。
60 ms 的触发间隔、35 ms 的事务超时和 500 cm 上限是本适配器的策略参数，
不代表在所有环境下都能达到 500 cm 测量精度，需按实车测量验证。

| 资源 | 用途 |
| --- | --- |
| PF11 | 推挽输出 TRIG，空闲低电平 |
| PF12 / EXTI12 | ECHO 双边沿输入，原厂默认不启用内部上下拉 |
| TIM7 | 1 MHz 计数，ARR=19，单脉冲更新中断后拉低 TRIG |
| DWT | 记录回波边沿 CPU 周期；使用 System.Init 已启用的计数器 |
| EXTI15_10_IRQn / TIM7_IRQn | 优先级 6，子优先级 0，不调用 RTOS API |

TIM6、UART、DMA 不占用。初始化检查 TIM7 时钟和 EXTI12 是否已经被占用；
GPIO 注册通过既有 BspGpio_InstRegister 检查冲突。
注册上下文为文件静态对象，ISR 只记录回波，任务通过短临界区取走完整快照。
若 GPIO 部分注册后失败，现有 GPIO BSP 没有注销接口；初始化失败应排除资源冲突后重启，
不把它当作支持任意热插拔的事务式初始化。

这次采用独立 BSP 配置已确认的原厂引脚及 TIM7，在指定注册位置调用；
不要求修改 Core 初始化顺序，也不新增 CubeMX 生成文件。
当前 `.ioc` 不记录本适配器占用的资源，后续分配必须同时检查此表。
以后用 CubeMX 配置 TIM7 或 EXTI 时，必须先协调中断函数和初始化的唯一所有者，
否则会出现重复定义或初始化失败；不得同时保留两套 IRQ 实现。

## 文件职责

| 新文件 | 职责 |
| --- | --- |
| `FrameComponets/Bsps/Inc/bsp_ultrasonic_echo.h` | 单设备回波采集接口和中断入口声明 |
| `FrameComponets/Bsps/Src/bsp_ultrasonic_echo.c` | PF11/PF12 初始化、TIM7 触发脉冲、EXTI 回波快照 |
| `FrameComponets/Mods/Inc/ultrasonic_factory.hpp` | 上层厘米接口和错误状态 |
| `FrameComponets/Mods/Src/ultrasonic_factory.cpp` | Reader 适配、超时/间隔/有效期检查、单位转换 |
| `tests/frame/ultrasonic_factory_tests.cpp` | 模块和原 Ultrasonic 包装层的主机测试 |
| `tests/frame/ultrasonic_echo_tests.cpp` | 实际新 BSP 与原 GPIO 分发的模拟寄存器测试 |
| `tests/frame/ultrasonic_mocks/` | 新 BSP 测试专用 HAL 模拟，不参与固件编译 |
| `tests/frame/Run-UltrasonicTests.ps1` | 新增测试入口，沿用 g++ 与原 out 目录 |

没有新增固件依赖。HAL、CMSIS、DWT、GPIO 注册机制全部复用当前工程。
没有重复定义 HAL_GPIO_EXTI_Callback；新 EXTI15_10_IRQHandler 将 10–15 线交给
HAL_GPIO_EXTI_IRQHandler，再由原 GPIO BSP 分发。TIM7 专用于该适配器。

## 接入点与最小修改方案

修改原因：独立文件不会自动执行，且 Keil 需要显式编译源文件。

1. `FrameComponets/Apps/Src/MainFrame.cpp` 增加 `ultrasonic_factory.hpp`，在原
   `BindHardware()` 内调用 `FactoryUltrasonic::Init(RangeSensor)`。
   失败时通过现有 `System.monitor.LogError` 记录一次；未绑定日志 Sink 时仍保持静默。
2. `.uvprojx` 的原 `Frame/Bsps` 与 `Frame/Mods` 分组分别增加一个源文件条目。
   保留编译器、宏、Include Paths、库、原文件条目及所有分组设置。

上述为最初的独立接入方案；当前已按统一临时测试方案接入超声波和OLED。
不再应用旧补丁，也不需要运行会重建整个分组的 Sync-FrameProject 脚本。

周期链路保持为：

```text
RobotSystemCpp -> FrameTickCpp -> Offroad.SampleSensors
              -> RangeSensor.Update -> 新 Reader -> BSP Poll / Start / Cancel
```

无需在 main、FreeRTOS 任务或 OffroadApp 中添加第二次调用。
Init 后等待 60 ms 发起首轮，后续两次触发起点至少间隔 60 ms。
每轮 Reader 都立即返回；没有等回波的循环、HAL_Delay 或 DWT 忙等。
TIM7 IRQ 结束 20 us 高脉冲不依赖任务及时再次运行，但实际脉宽仍受 IRQ 延迟影响。

兼容性影响：原 RangeSensor 从未绑定变为能采样；超声波有效样本参与原 WatchPoint。
电机和灰度仍未绑定时，自检仍不会通过，Offroad.Control 仍保持停车。
不修改挥手启动阈值，也不让有效距离自行启动小车。

## 上层接口

```cpp
#include "ultrasonic_factory.hpp"

float distance_cm = -1.0f;
FactoryUltrasonic::Status result = FactoryUltrasonic::ReadDistanceCm(distance_cm);
if (result == FactoryUltrasonic::Status::Ok)
{
    // distance_cm 单位为厘米；在这里交给上层策略。
}
else
{
    // 不使用 distance_cm 控制运动；此时输出固定为 -1.0f。
}
```

`Init(Ultrasonic&)` 返回 bool，只支持原厂单个设备；重复初始化同一个设备不会重复注册，
不同设备或已经被其他驱动绑定的设备会被拒绝。所有公开调用在框架任务中执行。
上层读取只返回缓存和状态，不发起测量、不推进采集，也不会刷新时间戳。

| Status | 含义 |
| --- | --- |
| NotInitialized | 尚未初始化 |
| Waiting | 初始化成功，尚无完成结果 |
| Ok | 有新鲜有效距离；等待下一次回波期间可继续读取上次有效结果 |
| Timeout | 发起后 35 ms 无完整回波；旧样本立即作废 |
| InvalidEcho | 零脉宽、超过 500 cm、晚到或底层标记无效 |
| HardwareError | 初始化/触发失败，例如资源占用或 ECHO 启动前一直为高 |
| Stale | 上次有效样本超过 200 ms 未更新 |

除了 Ok，distance_cm 都是 -1。超时后驱动会在满足 60 ms 间隔时自动重试。
事务起止使用无符号计时差，支持 HAL tick / DWT 单次回绕。
新失败结果通过原 Reader 的 `true + valid=false` 语义立即清除样本有效性；
无新结果返回 false，不伪造新时间戳。
原 `Ultrasonic::Sample.distance_mm` 仍以毫米存储，厘米只在新增读取接口输出。

## 验证

现有脚本保持原样，新增测试仍使用相同 g++ 模式：

```powershell
.\tests\frame\Run-Tests.ps1
.\tests\frame\Run-UltrasonicTests.ps1
.\tools\Format-Code.ps1 -Check
```

本次环境没有可发现的 g++ / clang-format，使用本机 MSVC 14.44 对测试源进行了补充验证：
模块测试和 BSP 模拟测试通过 `/W4 /WX` 编译并执行通过；
原 frame_tests 也执行通过，其既有算法代码存在 MSVC 浮点转换警告。
这不等同于原 g++ 脚本、clang-format 检查或 Keil V6 完整固件验证通过。

新增测试覆盖厘米/毫米换算、首轮等待、触发节流、35 ms 超时、无效数据、
500 cm 边界、200 ms 过期、失败后恢复、HAL tick 回绕，以及真实 BSP 的
TRIG 中断结束、双边沿捕获、DWT 回绕、取消后迟到边沿、资源冲突、临界区恢复和共享 EXTI 分发。

当前已获批准并接入，上板验证步骤：

1. Keil V6 执行完整 Rebuild，检查 0 Error / 0 Warning；确认两个新源文件参与编译，
   `TIM7_IRQHandler` 和 `EXTI15_10_IRQHandler` 只有一个强定义，原 GPIO HAL 回调唯一。
2. 保持停车，观察 Init 成功。用逻辑分析仪检查 PF11 空闲低、触发高约 20 us、
   相邻触发至少 60 ms；PF12 出现完整回波。不要在回波期间暂停 CPU 单步。
3. 使用垂直于探头的平整目标，分别放置在 10、20、50、100 cm，
   通过在同一框架任务的上层使用上述读取接口，或在调试器观察 `RangeSensor.GetSample()`，
   核对厘米输出约为毫米缓存的 0.1 倍，并记录误差与安装条件。
4. 撤走目标或改变角度制造无回波，确认约 35 ms 后获得 Timeout / -1，
   旧有效样本被清除；恢复目标后自动重新得到 Ok。
5. 验证故障期间主任务仍可执行其他采样；确认原自检/停车逻辑未被绕过。
   实测任务耗时和中断延迟，不把主机模拟当作传感器已实测通过。

已有调度配置风险仍保留：`INCLUDE_vTaskDelayUntil=0` 导致原 `osDelayUntil` 不延时。
本驱动使用 HAL 实际时间限制触发频率，不依赖循环次数，因而不会因该问题连续触发。
这不修复整车的周期/CPU 占用问题；其修复需要独立确认。

## 变更分类

- 新增内容：上述 BSP、设备适配器、测试、测试模拟、文档和待审批补丁。
- 修改内容：超声波和OLED已统一临时接入MainFrame与Keil工程，见临时测试说明。
- 可能受影响内容：PF11/PF12、TIM7、EXTI12/共享 IRQ、GPIO 注册容量、超声波自检数据和固件体积。
- 重点回归：原框架主机测试、超时停车链路、其他 EXTI 分发、V6 编译与实板采样/脉宽。
