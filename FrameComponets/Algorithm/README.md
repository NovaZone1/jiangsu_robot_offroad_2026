# Algorithm 层

完整移植 V1_main 的 6 个算法头文件及 5 个对应源文件，参考提交
`e403922005831e20a1f764486dffbcb64c216bb5`，保留原有类名和主要接口。
之前位于 Bsps 的 `pid/std_math/signator` 已迁入此目录；Keil 分组为 `Frame/Algorithm`。

| 文件 | 能力 |
| --- | --- |
| std_math.hpp / .cpp | 数学工具、Vec2/Vec3、颜色与数组工具 |
| pid.hpp / .cpp | 位置式/增量式 PID、前馈、限幅和死区 |
| signator.hpp / .cpp | 二/三阶 TD、低/高通滤波、摩擦补偿、方波、Kalman 和 IV 辨识 |
| linear_math.hpp | 固定尺寸矩阵、加减乘、数乘、转置、单位阵和求逆，模板只需头文件 |
| adrc.hpp / .cpp | ESO、非线性 TD、速度/位置 ADRC |
| hyperPID.hpp / .cpp | 上游未完成的 HyPID 参数容器，尚无控制计算实现；实际 PID 使用 PidGeneral |

目标为 STM32F103ZE / Cortex-M3，没有 FPU。已替换 F4 头文件并设置
`ARM_MATH_CM3`，矩阵调用工程已有的 `arm_cortexM3l_math.lib`，没有使用 F407 的浮点指令配置。
新增算法不自动接入电机控制循环，不改变现有默认停车行为。

适配时修复了会影响后续使用的问题：

- 矩阵求逆使用输入副本，防止 CMSIS 把 const 输入改写为工作缓冲；失败时保持输出不变。
- Kalman 使用本次传入的 control_input，并在校正后更新输出估计。
- IVIdentifier 初始化及 Reset 清空完整历史，保护采样频率、协方差分母和无激励输入。
- ESO/ADRC 初始化会清空状态，无效惯量/转矩常数/控制周期被拒绝；未初始化时返回零输出。
- 保留之前的 PID 初始化、零周期保护和滤波参数保护。

使用前按实际电机、减速比、采样频率及标定结果设置参数。ADRC 的输出单位为电流 A，
不能直接当作 `DcMotor::SetDuty()` 的占空比；必须根据实际驱动建立电流控制或转换环节。
`Observe()` 与 `Calc()` 的观测/计算流程按上游接口分开调用。

矩阵/Kalman 的临时矩阵会使用栈，建议先从低维模型开始，并在板上检查栈余量和运算耗时。
无 FPU 的 F103 不宜未经测量就把高维 Kalman 或完整 ADRC 放入 1 kHz 循环。
`DynamicArray` 仍是上游动态分配接口，当前比赛默认流程未使用它；优先使用固定容量数据结构。

验证入口：`tests/frame/Run-Tests.ps1`。主机矩阵测试编译的是工程中原有的
CMSIS DSP MatrixFunctions 源码，模拟头文件仅提供类型/函数声明。
Keil 完整重编译日志为 `MDK-ARM/build-frame-v6.log`。这些检查不替代实际硬件和控制参数调试。
