# PWM 减速电机开发说明

> 当前状态：电机与灰度 OLED 临时试验均已撤下，恢复原框架入口，四轮禁能。
> `MotorTest.hpp/.cpp` 已移至 `tests/frame/fixtures`，仅用于主机回归。
> 下文关于自动转动、MotorBench 和测试串口的段落是旧试验说明，不是当前入口。
> 当前操作见 [灰度测试](../../docs/gray_oled_test.md)。电机驱动参数与 API 保留。

当前适配 YB-DSF01 的 STM32F103ZET6、AT8236 双输入驱动和亚博 310 编码减速电机。
只参考 [Reactor70 的 MotorDJI 接口](https://github.com/njustup70/Reactor70/blob/master/Mods/inc/motor_dji.hpp)
（头文件 Git blob `278b63dcd0ccd9d8976b29a4d58766fb354f1f2d`），驱动和 PWM 调速实现为本项目编写。
没有移植 CAN、DJI 电流控制或位置控制。

本板的输出极性、编码器方向和死区补偿另对照用户提供的亚博 `car_tracking` 源码：
`C:/Users/Administrator/Desktop/STM32开发板小车/6、程序源码汇总/STM32_CAR源码汇总/STM32_CAR源码汇总/3.Smart_Car_Course/1.car_tracking/car_tracking`。
参考文件为 `BSP/motor/bsp_motor.c/.h`、`BSP/encoder/bsp_encoder.c`、`Core/Src/tim.c`，参考目录保持原样。
没有复制该工程的循迹、机械轮尺寸或 TIM6 调度；本项目保留 RPM 单位和 FreeRTOS 默认任务。

## 文件及职责

| 文件 | 职责 |
| --- | --- |
| `Mods/Inc/dc_motor.hpp`、`Mods/Src/dc_motor.cpp` | 电机生命周期、命令、限幅、故障、速度 PID；`MotorPWM` 是 `DcMotor` 的别名 |
| `Mods/Inc/motor_pwm_driver.hpp`、`Mods/Src/motor_pwm_driver.cpp` | AT8236 双 PWM、滑行停止、换向间隔、编码器输出轴 RPM |
| `Bsps/Inc/bsp_motor_board.h`、`Bsps/Src/bsp_motor_board.c` | 本板 GPIO、AFIO、20 kHz PWM 和四路编码器定时器初始化 |
| `Bsps/Src/bsp_tim_pwm.c`、`Bsps/Src/bsp_encoder.c` | 检查 HAL 返回值、双通道更新、计数回绕 |
| `Bsps/Inc/bsp_motor_console.h`、`Bsps/Src/bsp_motor_console.c` | 测试专用 USART1/PA9 非阻塞日志，板载 CH340，无需 ST-Link |
| `Apps/Src/MainFrame.cpp` | 四轮与 M1..M4 实际端口绑定，集中配置输出/编码器方向 |
| `Apps/Inc/MotorTest.hpp`、`Apps/Src/MotorTest.cpp` | 四轮速度试验参数及非阻塞 2/5/5 秒流程 |
| `Sys/Src/RtosCpp.cpp` | 每 1 ms 调用测试或比赛循环，通过编译开关选择 |

上述路径相对于 `FrameComponets`。电机仍由原框架任务控制，没有增加中断回调或第二个控制任务。

## 参数和接线

根据用户提供的 `Resource/原理图.pdf` 及
[亚博官方电机控制资料](https://www.yahboom.com/build?id=8924&cid=617)：

- 电机电源为 7.4 V，编码器供电范围 3.3–5 V；接到 STM32 的信号电平仍需按板卡接线确认。
- 310 电机减速比 20、编码器 13 线；本实现使用 A/B 双边沿四倍频，输出轴每圈有效计数为 `20 × 13 × 4 = 1040`。
- PWM 20 kHz；当前 72 MHz 定时器时钟下 `PSC=0`、`ARR=3599`，完全导通对应 `CCR=3600`。
- 编码器 `PSC=0`、`ARR=65535`、`TIM_ENCODERMODE_TI12`，输入数字滤波为 6。

| 端口 | IN1 / IN2 | PWM 通道 | 编码器 A / B | 编码器定时器 |
| --- | --- | --- | --- | --- |
| M1 | PC6 / PC7 | TIM8 CH1 / CH2 | PD12 / PD13 | TIM4 CH1 / CH2，全重映射 |
| M2 | PC8 / PC9 | TIM8 CH3 / CH4 | PA15 / PB3 | TIM2 CH1 / CH2，部分重映射 1 |
| M3 | PE9 / PE11 | TIM1 CH1 / CH2 | PA0 / PA1 | TIM5 CH1 / CH2 |
| M4 | PE13 / PE14 | TIM1 CH3 / CH4 | PB5 / PB4 | TIM3 CH2 / CH1，部分重映射 |

TIM1 全重映射到 PE，引脚 PA15/PB3/PB4 需要关闭 JTAG，代码保留 SWD。
M4 的 A/B 与 CH1/CH2 顺序相反，`BspMotorBoard_GetPort()` 用 `encoder_ab_swapped` 标记。
当前方向表已直接对照官方使用相同 TIM 原始计数的符号，包含实际布线；不再额外异或该标记。

根据用户确认的接线：`Motor1`/`LeftFrontMotor` 为 M1 左前，`Motor2`/`LeftRearMotor` 为 M2 左后，
`Motor3`/`RightFrontMotor` 为 M3 右前，`Motor4`/`RightRearMotor` 为 M4 右后。
旧 `LeftMotor`、`RightMotor` 名称分别引用左前 M1、右前 M3，不创建额外实例。
四路各有自己的适配器、反馈和 PID 状态。
`MainFrame.cpp` 中方向数组都按 M1..M4 排列，依据官方代码设置：
输出反向为 `{true, true, false, false}`，原始编码器计数反向为 `{false, false, true, true}`。
官方左侧将 PWM 取反、右侧保持，编码器累计则左侧取 TIM 正增量、右侧取负增量。
若电机接线被更改，仍须根据实测分别校正两组方向。
`output_reverse` 和 `encoder_reverse` 分别校正输出与反馈：车辆前进时，正占空比和正 RPM 必须一致。
不要在 PID 内用负增益补偿接线方向。

板级 BSP 已初始化四路所需定时器，四个适配器均已启动零占空比 PWM；电机模块仍保持禁能直到测试入口使能。
TIM1/8 及 TIM2/3/4/5 均保留给电机；TIM6 是 HAL 时间基准，其他成员不能重复占用。
此次没有改 `.ioc`，电机外设由 `BspMotorBoard_Init()` 初始化。
CubeMX 重新生成后保留框架初始化调用；不要再对这些资源运行另一套 `MX_TIM*_Init()` 或重映射。
若以后迁回 CubeMX 配置，须在同一变更中移除 BSP 的重复初始化并更新资源表。

## 接口约定

| 参考接口 | 本项目接口及行为 |
| --- | --- |
| `Init` | `Init(driver, Mode::Duty/Speed)`；重置限制和闭环配置，保持禁能；旧 `Bind()` 等同于 Duty 初始化 |
| `SwitchMode` | 先停止并禁能，切换后需要重新 `Enable()` |
| `SetSpeed` | 输出轴 RPM；Speed 模式缓存目标，由 `Control()` 执行闭环 |
| 电流输出/限幅 | 改为 `SetDuty([-1,1])` 和 `DutyLimSet((0,1])`，不提供电流控制接口 |
| `SpeedLimSet` | 输出轴 RPM 的绝对值上限，必须为有限正数 |
| `Neutral` | 清除目标和 PID 状态，输出双低滑行；保持使能状态 |
| `Disable` / `Enable` / `IsEnabled` | 禁能停止、显式使能、查询使能；使能本身不使电机转动 |
| `Control` / `ControlAllMotors` | 单电机或全部注册电机周期更新；注册上限 `FRAME_MAX_MOTORS=8` |

`Stop()` 是 `Neutral()` 的兼容入口。停止为滑行，不是主动刹车，不保证车轮立即静止。
采用快衰减 PWM：正向 IN1=PWM/IN2=0，反向 IN1=0/IN2=PWM，停止双低。
参见 [亚博驱动电路说明](https://www.yahboom.net/public/upload/upload-html/1740975408/Motor%20drive%20circuit%20design.html)。
程序不输出 IN1/IN2 双高的刹车状态。

默认换向滑行间隔为 2 ms，可以通过 `MotorPwmDriver::Config` 调整；这是软件间隔，仍需负载实测。
换向请求会先归零，后续 `Control()` 推进等待，不使用阻塞延时。
`SetDuty()` 成功表示接受指令；换向间隔内实际输出仍为零。
`DcMotor::GetDuty()` 是最近接受的限幅指令，`MotorPwmDriver::GetAppliedDuty()` 是当前实际施加的逻辑占空比。

官方 `MOTOR_IGNORE_PULSE=2000`、PWM 周期计数 3600，对非零输出添加带符号的 2000 死区补偿。
本板绑定因此设置 `deadzone_duty=2000/3600`：实际幅值为 `min(abs(control)+deadzone_duty, maximum_duty)`。
零指令始终为零，不添加偏置；负指令对称处理，补偿后的换向仍先滑行。
适配器通用默认补偿为零，只有本板绑定显式启用；`maximum_duty` 是实际 PWM 的独立上限，当前为 75%。
因此 `SetDuty()` / PID 返回的控制量在启用补偿后，不等于实际 PWM 占空比。
`DutyLimSet()` 限制控制量；查看实际输出请使用 `GetAppliedDuty()` 或串口的 `duty1000`。

驱动适配器检查重复 PWM 通道、共享编码器及 PWM/编码器定时器冲突；同一定时器的不同 PWM 通道组可以共存。
初始化失败会清零并关闭本次启动的通道；运行故障锁存资源，需显式释放后重建。
先 `DcMotor::Disable()`，再对适配器 `DeInit()` / `Init()`，最后重新 `DcMotor::Init()`。
析构也会停止并释放资源。定时器句柄和适配器必须比绑定的 `DcMotor` 活得更久；不能复制这些对象。
所有初始化、控制和资源释放由同一个任务执行，禁止 ISR 或并行任务改动输出、句柄或注册表。
不要绕过适配器直接写已经占用的 PWM/编码器；重复资源检查只覆盖本库内的适配器。
双通道写入会触发定时器 UG 立即提交预装载，不应让该定时器兼任其他有相位要求的设备。

## 反馈、超时与故障

每次 `Control()` 都采集计数，禁能时也采集；默认每 10 ms 产生一个新 RPM 样本：

`rpm = delta_counts × 60000 / counts_per_output_rev / elapsed_ms`

`ReadRpm()` 返回最近仍新鲜的 RPM，`GetMeasure()` 提供时间戳、样本编号和有效标记。
底层 `read_rpm` 回调只交付新样本，无新数据返回 `false`；每个新时间戳最多推进一次 PID。
默认命令超时 250 ms、反馈新鲜度 50 ms；在禁能时用 `SetTimeouts()` 调整。
使能后应周期性提交目标，不应只发送一次命令就永久运行。
目标过期、非有限命令、输出失败或速度模式反馈过期都会清除目标、禁能并停止。
`GetFault()` 可区分 `InvalidCommand`、`OutputFailure`、`FeedbackLost`、`CommandTimeout`。
排除原因后，禁能状态下 `ClearFault()`，再显式使能；它不会修复或重启已失败的硬件适配器。

计数相邻两次采样间的运动必须小于计数周期的一半，恰好半周期会报错。
一秒以上的 RPM 采样间隔会被适配器拒绝；更短的间隔也不能排除多圈丢计数，应维持 1 ms 轮询。
`IsOnline()` 只判断样本新鲜，静止时也有零 RPM 样本；它无法识别编码器断线、堵转或错误 CPR。
本板没有接入电机电流测量，因此库没有过流或堵转判定。

## 首次实物测试

**当前 `Bsps/Inc/frame_config.h` 的 `FRAME_MOTOR_TEST_ENABLED=1`，烧录/复位后会自动转动。**
原默认任务每毫秒调用 `MotorBench.Update()`，不运行比赛应用和传感器自检，不需要超声波/灰度假样本。
这是对原任务的分支选择，没有另加任务；不能同时运行两套控制。

| 从测试开始计时 | 阶段 | 四轮目标 |
| --- | --- | --- |
| 0–2 秒 | 等待四路新鲜编码器反馈 | 禁能、输出为零 |
| 2–7 秒 | 前进 | 每轮 +60 RPM |
| 7–12 秒 | 后退 | 每轮 -60 RPM，先清除前进积分，硬件保留 2 ms 换向间隔 |
| 12 秒以后 | 完成 | 四轮禁能、双低滑行，保持停止指令 |

每次上电只运行一次，完成或故障后不自动重试；复位会重新执行。
5 秒表示目标下发阶段的长度，实际加减速需要时间；滑行停止后车轮不会保证立即静止。
先架空四轮，并接好四路电机电源和编码器再复位。
第一次观察每轮的物理前进方向和正负 RPM，方向不符时断开电机电源，分别修正 `MainFrame.cpp` 的方向数组。
正确符号应满足：目标为正、物理轮子向车辆前进方向转动、反馈 RPM 为正；不能仅凭 PID 没有报错判定方向正确。

任一路初始化/使能/输出失败、反馈过期、持续无计数或方向错误，都会禁能四轮。
无计数检测：占空比指令绝对值至少 10%，连续 1.5 秒没有达到 3 RPM 的新样本。
方向检测：每个行驶阶段先留 500 ms 加减速时间，之后连续三个新样本与目标方向相反则停止。
两项检测是本次试验的辅助检查，不能代替电流保护或证明传感器线路正常。
运行阶段如果控制调用间隔达到 50 ms，恢复调用时停止四轮，不刷新目标掩盖超时。
该检查依赖任务继续运行；调试器暂停 CPU 时不能保证输出自动归零，电机有电时不要打断点暂停控制循环。

Keil Watch 中可展开 `MotorBench` 查看 `status_`：

- `phase`：等待反馈、前进、后退、完成或失败。
- `failure`：配置、反馈、使能、输出/模块故障、控制超时、方向错误或无计数。
- `failed_port`：出错端口 1..4，0 表示整体错误；`elapsed_ms` 为当前阶段计时。
- `Motor1`..`Motor4` 的 `measure_`：输出轴 RPM、时间戳、样本编号；`fault_` 为模块具体故障。
- `Motor1Driver`..`Motor4Driver` 的 `applied_duty_`：当前实际施加的逻辑占空比。

若提前停止，先读取以上状态，核对供电、计数、方向和连接；不要直接增大 PID 试图绕过错误。
完成架空测试后，再做负载实测和逐轮调参。
恢复比赛模式时将 `FRAME_MOTOR_TEST_ENABLED` 改为 0 后重新编译；比赛应用仍保持停车，四轮策略尚待集成。

## 当前试验 PID 与后续调参

速度环已经复用 `Algorithm/PidGeneral`，有占空比限幅和饱和时停止继续积分的处理。
通用库默认增益仍为零；测试入口根据用户要求显式使用以下试验值，各轮状态互不共享：

| 参数 | 试验初值 |
| --- | --- |
| Kp / Ki / Kd | 0.0008 / 0.005 / 0 |
| 目标转速 / 目标限幅 | ±60 RPM / ±80 RPM |
| PID 控制量限幅 / 积分项限幅 | ±0.15 / ±0.10 |
| 实际 PWM 限幅 | 本测试约 ±0.7056；驱动硬上限 ±0.75 |
| 反馈周期 / 新鲜度 / 命令超时 | 10 ms / 50 ms / 250 ms |

参数位于 `Apps/Inc/MotorTest.hpp` 的 `Config`。
这些增益是保守的试验起点，不来自电机厂家、不代表实测调参结果；Kd 暂为零，避免首次测试放大计数量化噪声。
静止时 60 RPM 误差对应约 4.8% 比例控制量，加 55.56% 补偿后实际 PWM 约 60.36%。
上一版未添加官方死区补偿，30% 实际输出上限低于参考代码的 55.56% 补偿基值，已修正。
官方增量 PID 使用 mm/s 误差、PWM 计数和固定 10 ms 周期，不能把其 0.8/0.06/0.5 原值直接填入本库。
本库使用 RPM 误差和秒、归一化控制量；当前参数只是重新选择的试验值，不保证负载下能达到目标。
主机测试使用简化惯性模型，仅验证程序和阶段流程，不能证明真实电机闭环稳定或速度准确。
速度模式使能要求：有编码器、新鲜反馈、已设置速度上限，以及至少一个非零的 PID 增益。
增益单位基于 RPM 误差和秒：输出为归一化占空比，积分限幅也采用占空比单位。

后续调整参数时，在禁能状态调用 `ConfigureSpeedPid()` 和 `SpeedLimSet()`，
调用 `SwitchMode(Mode::Speed)`，继续轮询至 `IsOnline()` 为真，再 `Enable()` 并周期性 `SetSpeed()`。
方向符号必须先验证一致；切换模式/清除故障不会自动恢复目标。
重新 `Init()` 会清除原设备的 PID 配置、速度上限、占空比限制和超时配置，需要重新设置。

## 软件验证

主机测试覆盖实际 PWM/编码器 BSP、换向等待、tick/计数回绕、独立通道、资源冲突、释放重绑、
HAL 启动失败、默认禁能、指令超时、反馈过期、非有限输入、PID 使能条件和饱和积分处理。
四轮试验另覆盖阶段计时、全部轮子限幅、只运行一次、任一轮故障使四轮停止、方向错误、无计数及控制间隔超时。
运行 `tests/frame/Run-Tests.ps1`；正常固件使用 Keil ARM Compiler V6 Rebuild。
本次编译日志为 `MDK-ARM/build-motor-test-v6.log`，生成文件仍在 `MDK-ARM/jiangsu_robot_offroad_2026/`。
软件测试不代替板上 PWM 波形、实际接线、编码器精度、实时性或电机负载验证。

## 没有 ST-Link 时通过串口排查

FlyMCU 下载后退出或释放 COM 端口，再用串口助手打开同一个板载 CH340 串口，设置 **115200、8N1、无流控、文本接收**。
先架空四轮，打开电机电源，再按板上的 RESET。串口助手不要启用自动下载或持续控制 DTR/RTS。
亚博的 [下载说明](https://www.yahboom.net/public/upload/upload-html/1740658131/Program%20download%20and%20simulation.html)
要求 FlyMCU 使用“DTR 低电平复位，RTS 高电平进入 BootLoader”，下载完成后按 RESET 运行。
也可检查 FlyMCU 的“编程后执行”；不要只依据写入成功判断用户程序已开始运行。

串口日志持续重复状态，即使测试已经失败也能看到，不需要在 12 秒内抢读：

```text
BOOT MOTOR_TEST_4W_DZ2000_UART1_115200 waiting=2s forward=5s reverse=5s
HW step=READY port=1
...
TEST phase=WAIT failure=NONE port=0 ms=... en=0 online=15 rpm10=0,0,0,0 duty1000=0,0,0,0
```

`rpm10` 为 RPM 乘 10，`duty1000` 为补偿后的实际占空比乘 1000，按 M1..M4 排列。
`en` 和 `online` 是四路位图：15 表示四路都使能/反馈都新鲜，0 表示全部禁能/没有新鲜反馈。
输出使用固定容量队列和 TXE 非阻塞轮询；队列满时丢弃整行，不能拖慢电机任务。
USART1/PA9 只在电机测试模式初始化，不能同时绑定其他 UART1 驱动。

| 日志 | 优先排查 |
| --- | --- |
| 完全没有 `BOOT` 或 `TEST` | COM/波特率、HEX 是否为本次版本、RESET/BOOT0 状态，用户程序或任务是否启动 |
| `HW step=...FAILED` | 板级定时器或具体端口初始化失败 |
| `failure=SETUP/ENABLE` | 对应端口未就绪、PID 配置或使能条件失败 |
| `failure=FEEDBACK` | 反馈采集失败/过期；零转速也会交付有效样本，不能把它直接解释为编码器断线 |
| `failure=NO_MOTION` | 电机电源/开关、接线、驱动力或编码器不产生计数；四轮因此停止 |
| `failure=WRONG_DIRECTION` | 输出或编码器符号不匹配，查看 `port` 后单独校正 |
| `phase=FORWARD` 且 PWM 非零，但轮子不转 | 检查 VM 电源、驱动输出和电机连接；不能仅凭 MCU 串口/PWM 数据证明驱动功率级有电 |

请保存从复位开始的日志后再调整参数；不要为排查而删除全部停机保护。
