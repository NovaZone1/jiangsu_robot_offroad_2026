# M3 右前轮本地持续 150 RPM 测试

当前选择 `FrameComponets/Apps/Inc/MotorContinuousTest.hpp` 中的
`MOTOR_CONTINUOUS_TEST=1`、`SelectedPort=3`。这是会自动启动右前轮的临时台架固件。

## 操作

1. 断电后将右前轮的电机线和编码器线接板上 M3，装好该路电驱，固定车体并架空车轮。
   M3 使用 PE9/PE11、TIM1 CH1/CH2，编码器 PA0/PA1、TIM5；沿用原已确认方向。
   关闭 Nova 的试验和串口。
2. DAPLink 下载重新编译的 Keil 工程，或使用 `MDK-ARM/m3_continuous_150rpm.hex`。
   新增了工程文件，已经打开的 Keil 工程需重新载入。
3. 上电/RESET 后约 2 秒，停稳检查通过后，仅 M3 按前进方向以 **150 RPM 目标**持续闭环运行。
   不需要蓝牙桥接、串口命令或 Nova；正常运行没有定时终止，也不自动反转。
4. 结束时关闭电机电源。RESET 会重新开始该测试，不是永久停止按钮。

150 RPM 是目标，不保证在实际负载和占空比限幅下达到。初值 Kp=0.0012、Ki=0.008、Kd=0，
沿用 Nova 试验的 D-on-measurement 控制路径、0.03 s 测速滤波和 50% PWM/积分限幅。
该版本只是排除上位机与桥接因素，不代表已经修好此前不转的问题。

## 保留的保护

M1/M2/M4 每周期禁能。仍使用原 `DcMotor`、`MotorPwmDriver`、`MotorProtection`：
方向检查、无运动约 1.5 秒、超速、反馈失效、驱动失败、命令超时、50 ms 调度间断、
换向死区和原停稳互锁全部保留，阈值不改。故障禁能且锁定，不自动再次使能。
控制任务每周期提交本地目标；任务停止更新时原调度/命令保护仍在。
Nova 不初始化、不调度，因此本地运行不要求远程心跳，也没有 Nova 单次试验 45 秒限制；
Nova 原实现未修改，恢复其模式后这些通信保护仍生效。

## 不转或中途停止时

停止后不要先 RESET，在当前 Keil 会话读取：

| Watch 表达式 | 含义 |
| --- | --- |
| `MotorBench.status_.phase` | Disarmed=0，Waiting=1，Running=2，Fault=3 |
| `MotorBench.status_.failure` | 本地入口失败原因（Protection=2 表示原保护触发） |
| `MotorSafety.status_.fault` | 原保护具体原因，NoMotion=6 |
| `MotorSafety.status_.failed_port` | 故障端口，3 为 M3 |
| `MotorBench.status_.port` | 本固件为 3，即右前轮 |
| `MotorBench.status_.target_rpm` | 最后一次使能控制时的目标 |
| `MotorBench.status_.measured_rpm` | 最后一次使能控制时的原始实测转速 |
| `MotorBench.status_.duty` | 最后一次使能控制接受的占空比指令，不等于实测电压 |
| `Motor3.fault_` | 电机模块故障 |

状态快照在保护停车后保留，便于判断有目标但无反馈。Keil 暂停会干扰实时控制，不要用断点
代替停车；重新运行后调度保护可能触发。主机测试不代表硬件 PWM/电驱已验证正常。

## 恢复其他固件

- 恢复 Nova：`MOTOR_CONTINUOUS_TEST=0`，保留 `NOVA_M1_TUNING=1`，重新编译烧录。
- 恢复比赛：上述两个宏均设为 0，重新编译并按比赛应用原校准/自检/启动流程验证。

本地模式优先于 Nova 开关，不会让两者同时写电机。没有改动其他轮子的方向或底层保护。

## 2026-10-11 本地存档状态

当前 M3 仍未转动，尚未定位根因；本提交是排查检查点，不是已修复版本。
Keil V6 完整编译为 0 Error / 0 Warning，主机回归通过。
实物 Watch：`MotorSafety.status_.fault=6 NoMotion`、`failed_port=3`；
`MotorBench.status_` 的 port=3、target_rpm=150、measured_rpm=0、duty=0.5；
`Motor3.fault_=None`。软件占空比不能证明物理 PWM 或电驱输出正常。
用户测得接口第 2 脚对 GND 约 11.97 V、第 3 脚约 3.27 V、第 6/7 脚间 0 V；
尚需核实 M3 的 6/7 是否为输出端，以及是否在保护停车前测量。此前 PCB 图属于左前轮。
与此前保留的电机配置相比，本地测试采用 Nova 的 PID 计算分支及 50% 限幅，
尚未执行恢复原 PID 路径和 69% 限幅的对照试验。后续排查继续保留原保护。
