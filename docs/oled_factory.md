> 当前状态：超声波与灰度 OLED 临时测试均已撤下。OLED 驱动保留但不参与当前固件编译；[灰度测试记录](gray_oled_test.md)保留用户反馈。下文测距显示接入步骤仅为历史说明。
> 专用RangeDisplayApp已从FrameComponets移出，仅保留在tests/frame/fixtures供主机回归测试使用。可复用驱动保留，未参与当前固件编译。不要重新应用旧接入补丁；不要用Sync-FrameProject将暂存驱动重新加入工程。用户的Keil版本及RTE设置保留。

# 原厂 OLED 显示超声波距离

## 当前交付状态

用户已批准临时测试接入：当前 MainFrame 已绑定超声波并注册 OLED 显示应用，
Keil 工程已加入所需源文件。测试保持电机停车，不绕过原自检。
临时入口以 `TEMP_RANGE_OLED_TEST` 标记，移除步骤见 `docs/range_oled_temporary_test.md`。
`docs/range_oled.integration.patch` 已更新为当前接入差异，可用于反向移除；
两份 integration.patch 均不要再次应用到当前工程。

## 原厂依据

配套资料 `STM32开发板小车/6、程序源码汇总/STM32_CAR源码汇总.zip` 中：

`STM32_CAR源码汇总/2.Extended_Course/4.OLED_Show_lightseeking/OLED_Show_lightseeking/`

- `BSP/OLED/bsp_oled.h` 指定 SSD1306、128x32、I2C1、7 位地址 0x3C。
- `Core/Src/i2c.c` 指定 PB6/SCL、PB7/SDA，开漏复用，400 kHz。
- `BSP/OLED/bsp_oled.c` 提供 32 行复用、COM 配置、扫描方向和电荷泵命令。

本实现沿用这些硬件参数及命令含义，重新编写异步发送与显示状态机。
原示例的无超时等待、HAL_Delay(100)、8 页刷新不直接搬入当前任务；
128x32 只需要 4 页、512 bytes。字符为本模块定义的紧凑 5x7 数字/英文字形，
没有引入原示例的外部字体库或新的固件依赖。

## 新增文件与职责

| 文件 | 职责 |
| --- | --- |
| `Bsps/Inc/bsp_oled_bus.h`、`Bsps/Src/bsp_oled_bus.c` | I2C1 初始化、异步发送、错误和超时恢复 |
| `Mods/Inc/oled_factory.hpp`、`Mods/Src/oled_factory.cpp` | SSD1306 命令、512-byte 显存、文本和分步刷新 |
| `Apps/Inc/RangeDisplayApp.hpp`、`Apps/Src/RangeDisplayApp.cpp` | 读取厘米接口、显示数值/错误、应用生命周期 |
| `tests/frame/oled_factory_tests.cpp`、`oled_mocks/` | 模拟 I2C 中断及距离到显示的联动测试 |
| `tests/frame/Run-OledTests.ps1` | 沿用 g++ 的新增测试入口 |

前三行路径均相对于 `FrameComponets`。测试 mock 仅用于主机，不加入固件。

## 接入方案与兼容性

接入仅涉及两个既有文件：

1. `Apps/Src/MainFrame.cpp`：增加两个头文件，在 `BindHardware()` 初始化原厂超声波，
   在 `MainFrameCpp()` 原 Offroad 注册之后注册 `RangeDisplay`，并标记临时测试块。
2. `.uvprojx`：在原 Bsps/Mods/Apps 分组中加入总计 5 个源文件条目，
   包含超声波 2 个、OLED 3 个。共 25 行新增。

OLED 初始化由现有 `System.RegistApp()` 调用 `Start()` 完成；
周期工作由 `System.UpdateApplications()` 调用 `Update()` 完成。
不增加任务、不修改主循环、OffroadApp、Ultrasonic 类、原初始化顺序、RTOS 配置或依赖管理。
显示应用继承默认 WatchPoint=true，显示故障只报告 App::Warning，
不替代传感器自检，也不因诊断屏故障新增整车停车条件。
原 Offroad 的传感器过期停车机制保持不变。

新资源占用：PB6/PB7、I2C1、I2C1_EV/ER IRQ（优先级 7）、两个 GPIO 注册槽、
一个应用注册槽、512-byte 显存及129-byte发送缓存。
超声波仍使用 PF11/PF12、TIM7、EXTI12，IRQ 优先级6，可抢占 OLED 发送。
本总线是 OLED 独占驱动，不是多个 I2C 设备的总线管理器。
仅使用一个 FactoryOled 实例，由 RangeDisplay 持有。

硬件初始化位于新 BSP 内；当前 `.ioc` 不记录新占用资源。
CubeMX 后续生成时必须同时参考这里的资源表，不再生成另一套 I2C1 初始化或同名中断。
初始化拒绝已启用的 I2C1、I2C1 引脚重映射及 BSP GPIO 注册冲突。
现有 GPIO 注册表不支持回滚，部分注册失败后应排除冲突并重启。

## 显示与运行行为

正常显示示例：

```text
ULTRASONIC
23.4 CM

OK
```

失败显示示例：

```text
ULTRASONIC
---.- CM

TIMEOUT
```

其他状态文本：NOT INIT、WAITING、INVALID、HW ERROR、STALE。
无效状态不继续显示旧距离。1 位小数仅是显示格式，不代表传感器具有 0.1 cm 精度。

- 上电等待100 ms通过时间戳判断，不调用延时函数。
- 文本最多每200 ms更新一次；实际可见更新还包含一轮分页发送耗时。
- 每次 Update 最多启动一次发送，发送由事件中断逐字节推进，不在任务中等待总线。
- I2C发送使用0x78写地址，命令控制字0x00、数据控制字0x40。
- 一页128 bytes，发送完成后才推进下一步；显存发送期间拒绝写入，防止混合帧。
- I2C事务30 ms未完成则复位外设并报告错误；运行中失败后1秒重试初始化和整屏刷新。
- 物理线路被持续拉低时不保证自动恢复，但会保持有界重试，不挂住控制任务。
- 不需要新增 HAL I2C 模块宏或 HAL I2C 源文件；使用现有设备寄存器定义及 GPIO HAL。
- 初始化/注册失败使用原 Monitor 一次性日志，未绑定 Sink 时不输出。

原框架 INCLUDE_vTaskDelayUntil=0 的问题未改动。
文本更新/上电等待/总线超时基于 HAL_GetTick，而不是循环次数；
仍需单独修复及实测整车实时性，不能将模拟测试当作1 ms调度保证。

## 接口

应用通常只需要注册全局 `RangeDisplay`，不直接操作显示对象。
设备层提供 `Init()`、`Update()`、`SetLine(row, text)`、`Refresh()`、
`IsBusy()`、`IsHealthy()`。row范围0–3，每行最多21个字符，超长文本截断；
ASCII小写显示为大写，不支持中文字库，未知字符显示问号。
SetLine 清空对应整行，避免从100.0变成9.0时残留字符。
所有普通调用由同一框架任务执行，ISR只操作底层传输。

## 验证方法

主机测试（需要 g++，可使用 -Compiler 指定路径）：

```powershell
.\tests\frame\Run-Tests.ps1
.\tests\frame\Run-UltrasonicTests.ps1
.\tests\frame\Run-OledTests.ps1
.\tools\Format-Code.ps1 -Check
```

本次使用本机 MSVC `/W4 /WX` 编译并运行 OLED 测试，通过：
I2C地址/数据复制、IRQ发送、NACK/仲裁丢失/总线错误、超时及tick回绕、
4页显存、发送期防改写、短文本清除残留、真实超声波适配器到显示像素、
超时清除距离、断屏降级与重试恢复。
当前缺少可发现的g++/clang-format，未宣称原脚本及格式工具检查通过。
尚未使用 Keil V6 完整构建或上板验证。

当前已接入，上板验证：

1. V6 Rebuild，确认0 Error/0 Warning以及I2C1_EV/ER、TIM7、EXTI15_10强定义唯一。
2. 车辆保持停车，上电应先出现WAITING，随后目标有回波时显示距离及OK。
   即使灰度/电机未绑定导致系统停留SELF_CHECK，显示应用也应持续更新。
3. 平整目标依次置于10、20、50、100 cm，观察厘米读数变化并记录偏差。
4. 移开目标或改变角度，应显示横杠及TIMEOUT/INVALID等状态，恢复目标后恢复数字。
5. 反复从较大距离切换到较小距离，确认末尾无旧字符残留。
6. 在断电条件下断开OLED，再上电验证超声波仍可采样、框架未卡死；
   原厂接线恢复后重新上电验证显示。运行中的通信故障恢复可用总线故障注入验证。
7. 逻辑分析仪确认PB6时钟不超过400 kHz，地址0x3C且有ACK；
   同时检查PF11约20 us触发、至少60 ms间隔，PF12回波未因刷屏丢失。
   不在回波期间使用暂停式断点判断性能。

## 本次变更分类

- 新增内容：OLED BSP、设备模块、RangeDisplay应用、模拟测试、文档及统一接入补丁。
- 修改内容：已接入 MainFrame 临时测试块及 Keil 工程的5个源文件条目。
- 可能受影响内容：I2C1及PB6/PB7、IRQ负载、GPIO/应用槽位、RAM/Flash、显示刷新耗时。
- 回归重点：原框架测试、超声波超时停车、EXTI回波时序、OLED离线不阻塞、V6完整编译。
