# jiangsu_robot_offroad_2026

## 多人协作

电机、超声波和灰度传感器的分工、驱动接口、硬件资源及交付要求见
[多人协作开发指南](AGENT.md)。开始模块开发前先阅读该指南。

根目录 `.gitignore` 已排除编译产物、测试输出和本机 IDE 配置；
CubeMX/Keil 工程文件及编译所需的 CMSIS 库保留在版本管理中。

## 越野比赛框架

已按 STM32F103ZE 移植 V1_main 相关代码至 `FrameComponets` 的
`Apps/Bsps/Mods/Sys/Algorithm` 五层目录，
电机已适配本板 AT8236 和亚博 310 编码减速电机，包含双 PWM、RPM 反馈和速度 PID 接口。
四轮已绑定：M1 左前、M2 左后、M3 右前、M4 右后，尚未实物验证。
**当前固件是电机测试模式，烧录/复位后会自动运行：等待 2 秒 → 前进 5 秒 → 后退 5 秒 → 禁能停止。**
目标 ±60 RPM，试验 PID 为 Kp=0.0008、Ki=0.005、Kd=0。
已对照用户提供的官方 `car_tracking` 修正方向及 2000/3600 死区补偿，测试实际 PWM 上限约 70.6%。
首次测试先架空四轮；板载串口输出 115200 8N1 的初始化和故障日志，无需 ST-Link。
详见 [电机说明](FrameComponets/Mods/MOTOR.md) 与 [框架说明](FrameComponets/README.md)。
超声波和灰度硬件驱动仍待完成。测试模式不执行比赛控制，也不等待传感器自检。
恢复比赛调度时，将 `FrameComponets/Bsps/Inc/frame_config.h` 的 `FRAME_MOTOR_TEST_ENABLED` 改为 0 并重新编译。

## 编译

使用 Keil µVision 打开 `MDK-ARM/jiangsu_robot_offroad_2026.uvprojx`，
选择 ARM Compiler V6，执行 Rebuild。

已使用本机 ARM Compiler V6.14.1 完整重编译验证：0 Error(s)，0 Warning(s)。
本次四轮测试版本的详细日志：`MDK-ARM/build-motor-test-v6.log`。
生成文件：

- `MDK-ARM/jiangsu_robot_offroad_2026/jiangsu_robot_offroad_2026.axf`
- `MDK-ARM/jiangsu_robot_offroad_2026/jiangsu_robot_offroad_2026.hex`

当前仅完成编译和链接验证，尚未烧录到开发板验证运行。

## V6 兼容性修复

原工程使用 FreeRTOS 的 `portable/RVDS/ARM_CM3` 移植层，包含 ARM Compiler V5
专用的 `__forceinline`、汇编块和内建函数，导致 V6 下大量重复报错。
现在工程的头文件搜索路径和 `port.c` 源文件路径均使用 `portable/GCC/ARM_CM3`。
该目录名称表示移植层采用 GNU 风格汇编；工程仍使用 ARM Compiler V6 编译。
两个新增文件来自 FreeRTOS 官方仓库，与现有内核版本 10.3.1 一致，保留原始许可：

- [port.c](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/V10.3.1-kernel-only/portable/GCC/ARM_CM3/port.c)
- [portmacro.h](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/V10.3.1-kernel-only/portable/GCC/ARM_CM3/portmacro.h)

原来的 RVDS 文件保留，但未参与当前 V6 工程编译。

原 CubeMX 配置 `ProjectManager.NoMain=true` 使 `main.c` 缺少入口函数，
导致链接错误 `Undefined symbol main`。已设为 `false`，并补齐 `main()`：
初始化 HAL、系统时钟、GPIO、FreeRTOS 默认任务，再启动调度器。

## CubeMX 重新生成工程

当前 CubeMX 导出目标为 `MDK-ARM V5.32`，重新生成可能恢复 RVDS 移植层路径
或覆盖 Keil 编译器设置。生成后检查：

1. Keil 的 ARM Compiler 仍选择 V6。
2. C/C++ 的 Include Paths 使用
   `../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3`。
3. 工程中的唯一 `port.c` 来自上述 GCC 目录，不能同时编译 RVDS 的 `port.c`。
4. 若新增的 GCC 目录被生成器删除，从上面的官方版本链接恢复两个文件。
5. CubeMX 保持取消勾选“不生成 main”，并执行一次完整 Rebuild。
