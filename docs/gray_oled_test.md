# 8-LP 八路 GPIO＋OLED 静止测试

> 当前状态：用户已反馈检测无误，要求删除测试部分。灰度显示对象、初始化、任务分支和四个
> Keil 编译项已撤下；GrayOledTest 移至 tests/frame/fixtures，仅供主机回归。
> 可复用灰度与 OLED 驱动未删除，已确认接线继续保留。当前恢复原框架入口，不恢复电机自动测试。
> 撤下后的构建日志为 MDK-ARM/build-gray-oled-removed-v6.log；新 HEX 仍在原输出位置。
> 尚未烧录撤下后的版本，开发板需重新烧录才能停止显示旧测试。以下是测试期间的历史说明。

## 测试期间的固件

用户已授权撤下电机测试、改为灰度 OLED 测试，并确认本文八路接线。
当前 `FRAME_GRAY_OLED_TEST_ENABLED=1`，`FRAME_MOTOR_TEST_ENABLED=0`。
原默认 RTOS 任务只推进灰度诊断；四个电机初始化后保持禁能，并在每次循环重新 Disable。
不发送速度/占空比指令，不执行比赛控制、超声波测距或旧 MotorBench 流程。
电机初始化失败也不阻断灰度显示。电机模块和驱动均保留。

灰度最小采样间隔 1 ms，OLED 目标刷新间隔 100 ms；显示传输异步进行。
该测试分支使用已有 osDelay(1)，实际周期包含任务执行与调度时间，不保证严格 1 kHz。
没有更改 RTOS 配置；比赛分支原 osDelayUntil / INCLUDE_vTaskDelayUntil=0 问题仍保留待处理。

## 接线

| 8-LP 通道 | STM32 GPIO | 屏幕位置 |
| --- | --- | --- |
| x1 | PC0 | X1-4 第 1 位 |
| x2 | PC1 | X1-4 第 2 位 |
| x3 | PC2 | X1-4 第 3 位 |
| x4 | PC3 | X1-4 第 4 位 |
| x5 | PA4 | X5-8 第 1 位 |
| x6 | PA5 | X5-8 第 2 位 |
| x7 | PB0 | X5-8 第 3 位 |
| x8 | PB1 | X5-8 第 4 位 |

按模块资料接 5 V 与公共 GND，核对实际板卡输出电平；不要将 5 V 电源直接接到 MCU 信号脚。
八路均为数字输入，不接 ADC、UART 或 I2C 灰度协议。
原厂 OLED 使用 PB6/SCL、PB7/SDA，I2C1、7 位地址 0x3C，SSD1306 128×32。
显示始终按 PCB 的 x1～x8 排列，不推断车辆左右方向。

灰度 GPIO 初始化集中在 `FrameComponets/Apps/Src/GrayOledTest.cpp`，
OLED 初始化沿用 `bsp_oled_bus.c`；均未迁移到 `.ioc`，避免重复初始化。
已核对这些引脚不与当前电机资源和保留的 PF11/PF12 超声波方案重叠。

## 屏幕与测试步骤

启动时第一行显示 `8LP RAW WARM 20S` 倒计时，其余两行立即显示原始电平；
等待至少 20 秒后进入下列显示。示例数字仅演示布局，不代表固定正确值：

```text
RAW CHECK KEY1
X1-4 1 1 0 0
X5-8 0 0 1 1
FLIP 11111111
```

- 每个数字就是一次 GPIO 读取的 0/1，无防抖、无模拟灰度值；屏幕只能展示刷新时刻的快照。
- `FLIP` 的八位同样按 x1～x8 排列。1 表示该路在预热完成后曾读到 0 和 1；0 表示尚未观察到两种电平。
- `FLIP` 不是校准状态，也不证明波形、电平或黑白判定正确。所有位为 1 后仍需逐路核对黑白极性。
- GPIO 初始化/注册失败时显示 `GRAY INIT ERROR` 和横杠，不以全 0 冒充测量。
- OLED 缺失或传输超时不阻塞采样；运行中总线故障采用原驱动的约 1 秒重试。

1. 在 Keil 中打开外层仓库的 `MDK-ARM/jiangsu_robot_offroad_2026.uvprojx`，V6 Rebuild 后按原方法烧录。
   当前生成的 HEX 位于 `MDK-ARM/jiangsu_robot_offroad_2026/jiangsu_robot_offroad_2026.hex`。
   只更改电脑上的代码不会停止开发板已运行的旧电机测试，需烧录新固件并复位。
2. 确认四轮保持停止，OLED 显示八路值。等待预热倒计时结束。
3. 首次使用或场地/高度变化时，长按模块 KEY1 至红灯常亮；实际使用高度下黑色材料静止 3 秒后短按，
   再对白色材料静止 3 秒后短按。红灯熄灭表示校准成功，慢闪则重做。
   校准期间屏幕仍显示电平，但这些值不能用来验收。
4. 用足够宽的白色材料覆盖八路，再覆盖黑色材料，每个位置保持至少约 0.5 秒观察。
   八路都应能稳定切换；再用窄黑条只覆盖某一路，确认变化位置与屏幕通道对应。
   通常黑低、白高，最终以校准后的实物验证为准；本屏幕不会人为反相掩盖接线差异。
5. 校准结束后建议连同主控一起复位，重新预热并做黑白测试，让 FLIP 记录不包含校准过程中的变化。
   单独复位模块无法由八路 GPIO 自动识别，软件预热倒计时也不会自行重启。
6. 某路始终为 0/1 时，依次检查对应针脚、共地、模块指示灯、安装高度和校准。
   断线的浮空输入也可能抖动，FLIP=1 不能用作断线检测。

此界面不调用 ConfirmCalibration(true)，不将诊断原始值当作比赛有效样本。
因此 GraySensor::valid 仍不被伪造为 true；测试不等于已经完成循迹集成。
原驱动校准步骤来源见 [8-LP 官方说明](https://www.yahboom.net/public/upload/upload-html/1731564712/8-channel%20line%20tracking%20module%20usage.html)。

## 变更分类与回归

- **新增内容**：GrayOledTest、GPIO 到 OLED 联动测试与独立测试入口、本文。
- **修改内容**：MainFrame、RtosCpp、测试模式配置、Keil 源文件清单、原测试入口及当前状态说明。
  电机 MotorTest 源码从 Apps 移至 `tests/frame/fixtures`，固件删除自动测试对象、调用和串口日志入口。
  Keil 移除 MotorTest / bsp_motor_console 编译项，仅加入 GrayOledTest、gray_yahboom_8lp、oled_factory、bsp_oled_bus。
  电机驱动、灰度驱动、OLED 驱动实现及超声波模块均未修改。
- **可能受影响内容**：默认任务诊断分支、8 路 GPIO 与 I2C1、中断负载、RAM/Flash、默认主机测试清单。
  原编译器选项、RTE、外设生成配置、依赖和嵌套仓库均未改动；没有运行全目录同步脚本。

验证入口：

```powershell
.\tests\frame\Run-Tests.ps1        # 原框架/电机回归，并执行新的灰度 OLED 联动测试
.\tests\frame\Run-Gray8LpTests.ps1 # 原八路驱动与循迹解释测试
# 仅运行新联动测试：
.\tests\frame\Run-GrayOledTests.ps1
```

联动测试编译真实 GPIO BSP、OLED BSP/模块、八路驱动和显示应用，通过模拟 I2C 中断检查屏幕像素。
覆盖全部 256 种输入组合、确认接线、预热计数回绕、翻转记录、初始化失败和 OLED 超时恢复。
其中 C 文件使用 gcc、C++ 使用 g++，保持 -Wall -Wextra -Werror，不修改生产驱动来规避 C++ 对 C 初始化的警告。
原 OLED/超声波独立脚本仍存在前次已知的 C 文件按 C++ 编译警告问题，本次未更改。

已通过上述主机测试及 ARM Compiler 6.14.1 的 Keil 全量 Rebuild，0 Error / 0 Warning。
日志：`MDK-ARM/build-gray-oled-v6.log`。尚未烧录、没有实物灰度或屏幕测试结论。
新增及修改的 C/C++ 文件用本机 clang-format 22.1.3 检查；仓库要求的 23+ 版本检查仍待补充。
重点实板回归：八路黑白翻转与针序、上电无电机动作、OLED 断开/恢复、实际刷新与采样间隔。
恢复比赛入口需经确认将 FRAME_GRAY_OLED_TEST_ENABLED 改为 0；不会恢复旧电机自动测试。
