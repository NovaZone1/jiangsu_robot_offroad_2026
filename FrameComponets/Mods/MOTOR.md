# PWM 减速电机开发说明

> 当前版本已移除电机自动试车、串口调参和专用串口代码，恢复框架调度，上电四轮禁能。
> 实测正反转 PI 位于 `Apps/Inc/MotorSpeedProfiles.hpp`，记录见 [速度 PI 实测](../../../docs/motor_pid_20261001/README.md)。

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
| `Apps/Src/MainFrame.cpp` | 四轮与 M1..M4 实际端口绑定，集中配置输出/编码器方向 |
| `Apps/Inc/MotorSpeedProfiles.hpp` | 八组实测正反转 PI、滤波和限幅，配置时保持禁能 |
| `Sys/Src/RtosCpp.cpp` | 每 1 ms 调用框架采样、状态机和电机控制 |

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

板级 BSP 已初始化四路所需定时器，四个适配器均已启动零占空比 PWM；电机模块保持禁能，只有应用可显式使能。
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

本板使用实际 PWM：`deadzone_duty=0`，实际输出上限为 0.69。
八组实测 PI 基于这一输出方式，不能叠加参考工程的 2000/3600 固定补偿。
适配器仍保留通用的死区补偿能力，供其他配置使用；本板当前不启用。
`DutyLimSet()` 限制控制量，实际 PWM 可从 `GetAppliedDuty()` 读取。

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

## 正式速度 PI 配置

`Apps/Inc/MotorSpeedProfiles.hpp` 按 M1～M4 保存前进、后退共八组实测参数。
本板初始化四轮驱动后，分别调用 `MotorSpeedProfiles::Apply(motor, port, false)` 配置前进参数，
保持禁能，没有自动目标或定时前进/后退流程。参数完整记录见 [实测数据](../../../docs/motor_pid_20261001/README.md)。

- PI 输入为输出轴 RPM，积分时间单位为秒，输出为实际 PWM 占空比；Kd=0。
- 输出限幅和积分限幅均为 0.69，反馈低通时间常数 0.03 秒。
- `GetMeasure().speed_rpm` 保留原始测速；`GetFilteredRpm()` 返回闭环使用的滤波值。
- 通用库的 `feedback_filter_tau_s=0` 表示不滤波，本板参数显式设置为 0.03 秒。
- 当前目标限幅为 160 RPM，只是软件命令边界。验证范围仍为架空 40/60 RPM，负载及更高转速待验证。
- `Apply` 只接受就绪、禁能的电机和端口 1～4，清除旧目标和积分，配置后不自动使能。

上层需要后退参数时，先提交停车并等车轮停稳，再禁能并切换。配置示例：

```cpp
Motor1.Disable();
if (!MotorSpeedProfiles::Apply(Motor1, 1, true))
{
    System.Stop(true);
    return;
}
// 后续由应用确认反馈新鲜，再显式 Enable，周期性 SetSpeed(负目标)。
```

不要每次更新目标都 Apply；会反复清积分，破坏速度环。
通用库默认增益为零，其他板级适配须自行配置；不能直接采用旧试验的猜测增益。
各轮反馈、PID 和故障独立，`MainFrame` 的系统停车回调禁能四轮。
临时试验中的方向/180 RPM 超速/无计数检测随试验入口撤下，正式控制应按比赛需要实现整车运行监测。
现有库仍保留命令过期、反馈过期、非法输入和输出失败停机，不含电流保护。

## 当前应用与验证

`RobotSystemCpp()` 只运行 `FrameTickCpp()`；没有测试分支，也不初始化电机专用 USART1。
`OffroadApp::Control()` 仍停车，传感器采集和比赛策略尚待正式集成；上电或复位不会自动试跑。
FlyMCU 下载仍使用原板载串口，当前固件不再输出 BOOT/TEST 调试日志。

主机回归覆盖双 PWM、换向等待、编码器回绕、资源冲突、HAL 失败、默认禁能、指令/反馈超时、
非有限输入、PID 饱和处理，以及实测参数配置和滤波保留原始测速。
运行 `tests/frame/Run-Tests.ps1`；固件使用 Keil ARM Compiler V6 Rebuild。
清理版本构建日志为 `MDK-ARM/build-clean-v6.log`，AXF/HEX 仍在正常 Keil 输出目录。
本次软件验证不能替代落地、四轮共同负载和比赛地面验证。
