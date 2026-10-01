# STM32F103ZE 越野比赛框架

移植参考为 [ZWJ_GeneralFramework 的 V1_main 分支](https://github.com/zwj051029/ZWJ_GeneralFramework/tree/V1_main)，
固定提交 `e403922005831e20a1f764486dffbcb64c216bb5`。
本地 `Resource/other_code/ZWJ_GeneralFramework` 与该提交一致；参考工程和比赛文件未修改。

目标芯片为 STM32F103ZET6，Cortex-M3，无 FPU，当前 HCLK 为 72 MHz，
Flash 512 KiB，RAM 64 KiB。工程仍使用 ARM Compiler V6。
框架已经接入 `main.c` 和 FreeRTOS 默认任务，不需要额外创建框架任务。

## 分层及移植清单

| 本工程目录 | 内容 | 来源及调整 |
| --- | --- | --- |
| Bsps | bsp_gpio | V1_main 的 Libs；适配 F1，检查注册容量、重复引脚和 EXTI 线冲突 |
| Bsps | bsp_tim_pwm | V1_main 的 Libs；区分 APB1/APB2、分频为 1 时不倍频，修复空指针及启停状态 |
| Bsps | bsp_dwt | V1_main 计时接口；改为当前 HCLK，原子累加计数差，修复溢出轮次乘 UINT32_MAX 的误差 |
| Bsps | bsp_uart | V1_main 注册及收发接口；去掉硬编码的灰度串口协议，注册不阻塞，检查缓存长度，提供 IT/普通 DMA 接收 |
| Bsps | bsp_adc、bsp_encoder | 新增底层支持，供模拟灰度及电机编码器模块使用；ADC 使用 F1 校准接口，编码器按 16 位定时器处理回绕 |
| Algorithm | std_math、pid | V1_main 数学、向量和 PID；使用软件浮点，补齐 PID 初始状态和零时间间隔保护 |
| Algorithm | signator、linear_math | 完整移植 TD、滤波、摩擦补偿、方波、Kalman、IV 辨识和矩阵工具；修复矩阵求逆改写输入、Kalman 忽略控制输入、IV 状态未初始化 |
| Algorithm | adrc、hyperPID | 移植 ESO/ADRC，补齐未初始化和无效物理参数保护；hyperPID 保留上游参数容器，尚无计算实现 |
| Bsps | std_cpp、frame_config | C/C++ 桥接和框架容量/周期配置 |
| Mods | std_actuator、led | V1_main 通用执行器及普通指示灯 |
| Mods | std_sensor | V1_main 通用 GPIO 传感器；改为固定容量，去除动态分配及旧 UART 灰度协议 |
| Mods / Bsps | dc_motor、motor_pwm_driver、bsp_motor_board | 本板 AT8236 双 PWM、310 编码器 RPM、独立速度 PID 和四路板级资源；待实物验证 |
| Mods | ultrasonic、gray_sensor | 模块接口和实现位置，硬件采集驱动待编写；未绑定时不伪造就绪状态 |
| Sys | StateCore、Action | 移植状态图、状态切换、动作超时/取消及非阻塞等待；修正当前状态复制、空图和空指针问题 |
| Sys | Application、System、Monitor、RtosCpp、SysDefs | 应用生命周期独立于系统调度；保留注册、自检、日志和任务桥接，使用 F103 比赛配置和单线程框架调度 |
| Apps | MainFrame、OffroadApp | 板级模块绑定入口、越野应用及比赛策略的扩展位置 |
| Apps | MotorTest | 独立四轮试验：等待 2 秒、前进 5 秒、后退 5 秒、禁能停止；当前已启用 |

`Libs → Bsps`；Algorithm 为独立层，算法头文件位于 `Algorithm/Inc`，
源文件位于 `Algorithm/Src`，原 Bsps 下的三个算法已经迁回 Algorithm，避免重复编译。
未移植 CAN、SPI、I2C、IMU、定位/视觉、机械臂、步进电机、舵机、气泵、继电器、
WS2812、远程控制/消息编码及空 Chassis 文件。
这些内容没有当前三类硬件的直接需求，或带有参考 F407 工程的板级依赖。

## 三类新模块的位置

- `Mods/Inc/dc_motor.hpp`、`Mods/Src/dc_motor.cpp`：生命周期、占空比和输出轴 RPM 速度 PID 接口。
  配套 `motor_pwm_driver` 和 `Bsps/bsp_motor_board` 已适配 AT8236 与 310 电机；详见 [电机说明](Mods/MOTOR.md)。
- `Mods/Inc/ultrasonic.hpp`、`Mods/Src/ultrasonic.cpp`：预留新测距数据及有效时间戳。
  需要按型号补充触发/回波捕获或串口协议、测量超时、触发间隔和距离换算。
- `Mods/Inc/gray_sensor.hpp`、`Mods/Src/gray_sensor.cpp`：预留最多 16 路采样及标定结果。
  需要按型号补充 GPIO/ADC/串口读取、白线与背景标定、通道位置。
  `GetLineError()` 只计算已标定数据的加权偏差，不代替实际采集驱动。

电机已有实际硬件驱动，超声波和灰度仍只有接口、参数检查及失效处理。
`Apps/Src/MainFrame.cpp` 集中绑定四轮：M1 左前、M2 左后、M3 右前、M4 右后。
底层初始化保持禁能，当前测试入口显式配置 PID，等待反馈后自动使能。
`FRAME_MOTOR_TEST_ENABLED=1` 使默认任务只运行 `MotorBench`，不执行比赛/传感器循环。
设为 0 后恢复比赛调度；传感器未绑定时仍保持自检等待。
方向和非零 PWM 死区补偿已对照用户提供的官方 `car_tracking`：左侧反向输出、右侧反向反馈。
实际接线仍需架空验证；测试 USART1/PA9 通过板载 CH340 输出启动和故障日志。
绑定的驱动、GPIO/UART 注册对象、应用和状态图必须保持静态或全程有效的生命周期。
初始化/注册只能在框架任务开始前完成，或由同一个框架任务执行。

## 与比赛规则的衔接

依据 `Resource/504+机器人越野+毛丽民+13814928578.docx`：准备时指示灯常亮，运行时有节奏闪烁。
该显示逻辑已接入 `System`，绑定真实 LED 引脚后生效。

以下策略在 `Apps/Src/OffroadApp.cpp` 明确留为 TODO，尚未实现比赛自动驾驶：

1. 使用新鲜超声波数据判断挥手启动和非接触停机，设置阈值、防抖及触发状态。
2. 白线循迹 PID、差速轮目标及可选编码器速度闭环。
3. 7 组虚线（50 mm 空白、100 mm 实线）的识别与短时保持策略，区别虚线空白和真正丢线。
4. 四类障碍及决赛悬崖的通过策略；当前资料的文字部分未给出全部障碍参数，须结合图和实车确认。
5. 起止黑线识别、起跑脱离判断、三圈计数，以及轮子越线后、触碰前方障碍前停车。

比赛模式的 `OffroadApp::Control()` 会保持停车，包括已绑定驱动、请求进入 WORKING 的情况，
直至真实控制策略完成。运行中关键传感器过期会取消动作、禁用状态机并调用电机停止接口。
灰度默认新鲜度阈值 50 ms，超声波 200 ms，实车时应根据采样周期调整。
规则中的比赛耗时/罚时由裁判记录，不能用传感器默认值假定已完成圈数或障碍。

## 调度与硬件约束

`main()` 在 HAL、时钟、GPIO 初始化后调用 `MainInitCpp()`。
`StartDefaultTask()` 调用 `RobotSystemCpp()`：1 ms 基础周期采样/控制，5 ms 更新应用、系统、状态机和动作。
所有框架注册表和应用由同一任务管理，避免从参考工程直接复制多个任务后产生数据竞争。
这是调度配置，尚未测量实板实时性能，Cortex-M3 的浮点运算为软件实现。

驱动 Reader 应非阻塞，只交付新数据；不得在每次调用时重新触发一次超声波测量。
按实际传感器要求自行管理触发间隔，并在中断里仅采集原始数据。
不要在 1 ms 控制循环调用阻塞日志、`HAL_Delay`、`Seq::Wait/WaitUntil`。
保留的 Seq 阻塞辅助函数只适用于独立的低优先级任务；当前框架应优先使用 Action 非阻塞接口。
Action 的 `Wait/WaitUntil` 必须传入 `blocked`、`seq_tick`，建议配合 `SEQLIZE/SEQPARAM`。

TIM6 专用于 HAL 时间基准，不能分配给电机、编码器或超声波。
电机 PWM/编码器已由 `BspMotorBoard_Init()` 初始化时钟、GPIO、AFIO 和定时器，不使用定时器中断。
TIM1/8、TIM2/3/4/5 保留给四路电机；此次 `.ioc` 未修改，不得重复生成/运行另一套电机初始化。
PWM ARR 建议不大于 65534，以便 CCR=ARR+1 表示完全导通；ARR=65535 时最大 CCR 为 65535。
ADC BSP 当前提供单通道单次读取，不等同于多通道扫描 DMA 驱动。
UART RX DMA 当前要求普通模式，循环 DMA 需另外实现增量索引处理。
日志 sink 应有界、非阻塞；未绑定时日志关闭。ISR 禁止调用 Monitor 或修改框架注册表。

DWT 至少每约 59 秒（72 MHz 下）采样一次以保留溢出信息，框架每毫秒维护。
变更系统时钟后必须重新初始化 DWT；低功耗暂停内核时不能把 DWT 当成墙钟。

FreeRTOS 默认任务栈改为 512 words（2048 bytes），heap_4 为 8192 bytes，
同时更新 `.ioc`。后续驱动和日志增加后需在板上检查栈余量。

## 代码规范与维护

五层框架和主机测试共用工程根目录的 `.clang-format` 与 `.editorconfig`：
4 空格缩进、Allman 大括号、每行最多 100 字符；条件和循环使用大括号，
函数之间留空行，避免把多个操作挤在同一行。文件使用 UTF-8 编码。

头文件声明接口，普通实现放在对应 `Src`，模板和简单访问器保留在头文件。
私有成员优先使用尾部下划线，避免双下划线及下划线加大写字母的保留标识符。
公开移植接口尽量保留原命名；应用更新入口已统一为 `System.UpdateApplications()`，
动作状态类型为 `ActionManager::ActionStatus`。
注释说明单位、约束和边界，硬件待实现部分用 `TODO` 标明。

`Application` 管理单个应用的生命周期，`System::Run()` 按顺序调用自检、启动判断、
健康检查、指示灯和日志。`MainFrame.cpp` 的 `BindHardware()` 集中放置板级绑定，
比赛应用的采样、控制和阶段更新分别放在 `OffroadApp` 的对应方法中。

在工程根目录执行（clang-format 23 或更新版本）：

```powershell
.\tools\Format-Code.ps1          # 格式化框架和测试
.\tools\Format-Code.ps1 -Check   # 只检查，不修改文件
```

脚本优先使用 PATH 中的 clang-format，其次查找 VS Code C/C++ 扩展自带版本；
也可通过 `-Formatter '完整路径\clang-format.exe'` 指定。
处理范围仅为 `FrameComponets` 和 `tests/frame` 中的 C/C++ 文件。

## 编译与验证

本次四轮测试版本 V6 完整 Rebuild 的日志位于 `MDK-ARM/build-motor-test-v6.log`。
AXF/HEX 位于 `MDK-ARM/jiangsu_robot_offroad_2026/`。
框架所有 Src 文件已加入 Keil 五个 `Frame/*` 分组，并增加 UART/ADC 所需的 F1 HAL 源文件。
电机按本板原理图配置资源；超声波和灰度仍等待确认并绑定各自的采集外设。

主机回归测试使用真实框架源文件及模拟时钟/RTOS接口：

```powershell
.\tests\frame\Run-Tests.ps1
```

覆盖矩阵/CMSIS 运算、求逆不修改输入、Kalman 控制输入、IV 重置、ADRC 初始化与限幅，
以及 DWT 溢出、PID 限幅/重置/零周期、状态机切换不覆盖原状态、动作超时/取消、
未绑定模块保护、数据过期停止和日志长度。
电机测试另覆盖实际双 PWM/编码器 BSP、换向与计数回绕、资源冲突/释放、HAL 失败、
禁能/超时保护及速度 PID 使能条件、饱和积分处理。
四轮试验另验证 2/5/5 秒阶段边界、只执行一次、四轮同时停机、方向错误和持续无计数。
主机测试不验证引脚、ADC 精度、编码器方向或实车性能。

CubeMX 重新生成后，在 Keil 关闭工程再执行：

```powershell
.\tools\Sync-FrameProject.ps1
```

脚本恢复框架源文件/Include Paths、HAL ADC/UART 编译宏及源码，并把 FreeRTOS 移植层恢复为 GCC/ARM_CM3。
它不会修改真实外设配置，也不会覆盖框架源文件或 `main/freertos` 用户代码。
如 CubeMX 删掉先前添加的 FreeRTOS GCC 移植目录，仍需先按项目根 README 恢复官方 10.3.1 文件。
重新生成后确认 ARM Compiler V6、512 words 任务栈、8192 bytes RTOS 堆及 USER CODE 区的初始化调用，再执行 Rebuild。
