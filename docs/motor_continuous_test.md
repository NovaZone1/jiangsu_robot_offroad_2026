# M1 左前轮本地持续 100 RPM 测试

> 历史测试记录：下文描述当时的测试固件，不是当前启动行为。当前已清理自动试转、默认关闭 Nova AI 试转；
> 上电禁能，原保护和四轮已调参数保留，见 [当前参数与交付说明](motor_pid_20261011.md)。
> `MotorContinuousTest` 已移至 `tests/frame/fixtures`，不参与固件编译；下文旧路径和宏只作历史参考。

> 当前固件已切换到 [M4 持续 100 RPM、Nova 只读监视](motor_m4_continuous_test.md)，本页保留作 M1 历史操作。
> 恢复本页模式需要重新选择端口/模式并编译；当前上电不会自动试转 M1。

本页历史模式选择 `FrameComponets/Apps/Inc/MotorContinuousTest.hpp` 中的
`MOTOR_CONTINUOUS_TEST=1`、`SelectedPort=1`。这是会自动启动左前轮的临时台架固件。

## 操作

1. 断电后将左前轮的电机线和编码器线接板上 M1，装好该路电驱，固定车体并架空车轮。
   M1 使用 PC6/PC7、TIM8 CH1/CH2，编码器 PD12/PD13、TIM4；沿用原已确认方向。
   关闭 Nova 的试验和串口。
2. DAPLink 下载重新编译的 Keil 工程，或使用 `MDK-ARM/m1_continuous_100rpm_tuned_nova_monitor.hex`。
   新增了工程文件，已经打开的 Keil 工程需重新载入。
3. 上电/RESET 后约 2 秒，停稳检查通过后，仅 M1 按前进方向以 **100 RPM 目标**持续闭环运行。
   不需要蓝牙桥接、串口命令或 Nova；正常运行没有定时终止，也不自动反转。
4. 结束时关闭电机电源。RESET 会重新开始该测试，不是永久停止按钮。

100 RPM 是目标，不保证在实际负载和占空比限幅下达到。初值 Kp=0.000957、Ki=0.009191、Kd=0，
沿用 Nova 试验的 D-on-measurement 控制路径、0.03 s 测速滤波；当前 PWM/积分限幅为 69%。
用户已确认 69% 限幅下 M3、M1 均可转动，具体转速精度与稳定性尚需通过实测曲线评估。

## 保留的保护

M2/M3/M4 每周期禁能。仍使用原 `DcMotor`、`MotorPwmDriver`、`MotorProtection`：
方向检查、无运动约 1.5 秒、超速、反馈失效、驱动失败、命令超时、50 ms 调度间断、
换向死区和原停稳互锁全部保留，阈值不改。故障禁能且锁定，不自动再次使能。
控制任务每周期提交本地目标；任务停止更新时原调度/命令保护仍在。
Nova 以只读监视模式初始化，不接管本地控制，因此本地运行不要求远程心跳，
也没有 Nova 单次试验 45 秒限制；恢复 Nova 调参模式后原通信保护仍生效。

## 在 Nova PID 页面看实时转速

1. 关闭 Nova 的旧串口连接和旧桥接，烧录 `m1_continuous_100rpm_tuned_nova_monitor.hex`。
   车轮保持架空；运行/RESET 后仍会在约 2 秒后自动试转。
2. 双击 `tools/ble_bridge/Start-Bridge.cmd`；看到 READY 后，Nova 连接
   **COM12、115200、8N1、无流控**。COM11 供桥接使用，COM5 是 DAPLink。
3. 串口收发页选字符串、CRLF，发送一次 `LOOP:1`，确认收到 `OK:LOOP:1`。
   这也会激活桥接转发；可再发送 `PID?` 查询当前参数。
4. 打开 PID 调参页，选择 **CH1**，取消“暂停显示”。绿色目标线为 100 RPM，
   红色实际线来自编码器测得并经现有 0.03 s 滤波的转速，不用目标代替实测。
   遥测 50 Hz，编码器/控制周期仍为原 10 ms；可勾选 Output 查看归一化占空比。
5. 目前只看曲线，不点击发送 PID 或启动 AI 试验。固件会拒绝 JSON/旧文本协议的
   PID 修改、目标修改和 configure/heartbeat，返回 `monitor_only`，不会改变电机参数。

断开 Nova、断开蓝牙或遥测拥塞时，本地 100 RPM 控制继续运行；原电机保护仍生效。
显式 STOP 仍有效：串口文本发送 `@{"cmd":"stop","id":"monitor-stop","channel":1}`
并带 CRLF，将立即禁能并锁定，包括启动延时期间收到 STOP 的情况，不会自动重启。
通信断开时不能依赖远程 STOP，结束测试直接关闭电机电源。重新 RESET 会重新开始测试。
保护触发或反馈无效时发送故障帧，不将失效数据伪装成正常轮速。

## 不转或中途停止时

停止后不要先 RESET，在当前 Keil 会话读取：

| Watch 表达式 | 含义 |
| --- | --- |
| `MotorBench.status_.phase` | Disarmed=0，Waiting=1，Running=2，Fault=3 |
| `MotorBench.status_.failure` | 本地入口失败原因（Protection=2 表示原保护触发） |
| `MotorSafety.status_.fault` | 原保护具体原因，NoMotion=6 |
| `MotorSafety.status_.failed_port` | 故障端口，1 为 M1 |
| `MotorBench.status_.port` | 本固件为 1，即左前轮 |
| `MotorBench.status_.target_rpm` | 最后一次使能控制时的目标 |
| `MotorBench.status_.measured_rpm` | 最后一次使能控制时的原始实测转速 |
| `MotorBench.status_.duty` | 最后一次使能控制接受的占空比指令，不等于实测电压 |
| `Motor1.fault_` | 电机模块故障 |

状态快照在保护停车后保留，便于判断有目标但无反馈。Keil 暂停会干扰实时控制，不要用断点
代替停车；重新运行后调度保护可能触发。主机测试不代表硬件 PWM/电驱已验证正常。

## 恢复其他固件

- 恢复 Nova：`MOTOR_CONTINUOUS_TEST=0`，保留 `NOVA_MOTOR_TUNING=1` 并设置 `NOVA_TUNING_PORT=1`，重新编译烧录。
- 恢复比赛：上述两个宏均设为 0，重新编译并按比赛应用原校准/自检/启动流程验证。

本地模式优先于 Nova 开关，不会让两者同时写电机。没有改动其他轮子的方向或底层保护。

## 2026-10-11 本地存档状态

存档当时 M3 仍未转动，尚未定位根因；该提交是排查检查点，不是已修复版本。
Keil V6 完整编译为 0 Error / 0 Warning，主机回归通过。
实物 Watch：`MotorSafety.status_.fault=6 NoMotion`、`failed_port=3`；
`MotorBench.status_` 的 port=3、target_rpm=100、measured_rpm=0、duty=0.5；
`Motor3.fault_=None`。软件占空比不能证明物理 PWM 或电驱输出正常。
用户测得接口第 2 脚对 GND 约 11.97 V、第 3 脚约 3.27 V、第 6/7 脚间 0 V；
尚需核实 M3 的 6/7 是否为输出端，以及是否在保护停车前测量。此前 PCB 图属于左前轮。
该存档当时采用 Nova 的 PID 计算分支及 50% 限幅。

## M3 的 69% 限幅对照结果

在本地 Git 存档 `1413c1b` 之后，仅将 M3 本地测试的 PWM 与积分限幅从 0.5 改为 0.69。
目标仍为 100 RPM，Kp=0.000957、Ki=0.009191、Kd=0，PID 算法、方向、2 秒启动延时、
其他轮子禁能及原保护阈值均不变。没有切换回原 PID 路径，方便单独判断限幅的影响。
烧录 `m3_continuous_100rpm_69pct.hex`；原 `m3_continuous_100rpm.hex` 保留为 50% 对照固件。
用户随后确认 M3 可以转动；尚未确认转速精度或稳定性。

## M1 对照结果与当前监视版本

在 M3 69% 版本基础上，仅将选中端口改为 M1；保持 100 RPM、当前 PID 算法与增益、
69% PWM/积分限幅及原保护不变，其他三轮禁能。使用 `m1_continuous_100rpm_69pct.hex`。
用户已确认 M1 能转动。当前在此基础上仅加入 Nova 只读监视，运行配置和保护不变，
新固件文件为 `m1_continuous_100rpm_tuned_nova_monitor.hex`，完整通信实测仍待用户烧录后验证。
