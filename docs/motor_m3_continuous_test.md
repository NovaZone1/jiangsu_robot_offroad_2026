# M3 右前轮持续 100 RPM，Nova 只读监视

> 历史测试记录：下文描述当时的测试固件，不是当前启动行为。当前已清理自动试转、默认关闭 Nova AI 试转；
> 上电禁能，原保护和四轮已调参数保留，见 [当前参数与交付说明](motor_pid_20261011.md)。
> `MotorContinuousTest` 已移至 `tests/frame/fixtures`，不参与固件编译；下文旧路径和宏只作历史参考。

> 当前已切换为 [M4 持续 100 RPM、Nova 只读监视](motor_m4_continuous_test.md)，M3 保持禁能。下文为此前 M3 持续运行固件的操作，不适用于当前工程烧录结果。

本页历史模式选择 `MOTOR_CONTINUOUS_TEST=1`、`SelectedPort=3`，上电/RESET 约 2 秒且停稳检查通过后，
右前轮 M3 自动以 100 RPM 目标持续正转。正常运行没有定时终止，也不自动反转。
M1/M2/M4 每周期保持禁能。

## 已固化参数

| 项目 | 设置 |
| --- | --- |
| M3 正转 Kp / Ki / Kd | 0.001176 / 0.013926 / 0 |
| M1 正转 Kp / Ki / Kd（保留、禁能） | 0.000957 / 0.009191 / 0 |
| PWM / 积分限幅 | 0.69 / 0.69 |
| 编码器 | 30:1，11 线，四倍频 1320 CPR |
| 测速滤波 / 控制周期 / 遥测周期 | 0.03 s / 10 ms / 20 ms |

新参数依据见 [M3 第二次调参记录](motor_pid_20261011_m3/session2/README.md)。
本次不改变电机驱动、方向或保护阈值。方向异常、无转速、超速、反馈/驱动失败、命令超时、
50 ms 调度间断、停稳互锁及故障锁定均保留，故障时禁能四轮，不自动重启。
本地主动更新目标，Nova 不接管控制，因此持续模式不需要远程心跳；AI 模式的通信保护代码保留。

## 烧录及查看曲线

1. 关闭 Nova 串口和旧桥接。断电后确认电机线及编码器线接 M3，装好该路电驱，固定车体、架空车轮。
   M3 PWM 为 PE9/PE11（TIM1 CH1/CH2），编码器为 PA0/PA1（TIM5），沿用已验证方向。
2. 使用 DAPLink 烧录 `MDK-ARM/m3_continuous_100rpm_tuned_v2_nova_monitor.hex`，运行并按 RESET。
   约 2 秒后 M3 自动运行，无需 Nova 命令。RESET 是重新开始测试，不是持续停止。
3. 运行 `tools/ble_bridge/Restart-Bridge.cmd`，按窗口提示等待 READY。
   Nova 打开 COM12，115200、8N1、无流控；COM11 留给桥接，COM5 为 DAPLink。
4. 串口收发页选择“字符串”和 CRLF，发送一次 `LOOP:3`，确认 `OK:LOOP:3`。
   再发 `PID?`，应返回 `PID:0.001176,0.013926,0.000000`。
5. PID 调参页面选择 CH3，取消“暂停显示”。目标线为 100 RPM，实际线为编码器测得的滤波转速。
   可勾选 Output 看占空比，稳定后采集至少 10 秒并导出 CSV。
   当前只看曲线；固件拒绝修改 PID、目标以及启动 AI 试验，返回 `monitor_only`。

蓝牙/Nova 断开或遥测拥塞不会停止本地持续运行。结束测试可关闭电机电源；
连接正常时也可字符串、CRLF 发送 `@{"cmd":"stop","id":"monitor-stop","channel":3}`，
电机立即禁能并锁定，RESET 后才会重新开始。启动延时期间收到 STOP 同样有效。

## 异常时查看

停止后先保留现场，读取 Keil Watch：`MotorSafety.status_.fault`、`MotorSafety.status_.failed_port`、
`MotorBench.status_.phase`、`MotorBench.status_.failure`、`MotorBench.status_.duty`。
M3 的端口号应为 3；NoMotion=6 表示没有检测到有效转速，不能通过取消保护继续试跑。

若以后恢复 M3 AI 调参，改为 `MOTOR_CONTINUOUS_TEST=0`，保持 `NOVA_MOTOR_TUNING=1`、
`NOVA_TUNING_PORT=3` 后重新编译；档案基准须使用新 PID，避免旧档案覆盖本次固化结果。
