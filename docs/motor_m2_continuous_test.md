# M2 左后轮持续 100 RPM，Nova 只读监视

> 历史测试记录：下文描述当时的测试固件，不是当前启动行为。当前已清理自动试转、默认关闭 Nova AI 试转；
> 上电禁能，原保护和四轮已调参数保留，见 [当前参数与交付说明](motor_pid_20261011.md)。
> `MotorContinuousTest` 已移至 `tests/frame/fixtures`，不参与固件编译；下文旧路径和宏只作历史参考。

> 当前已切换为 [M4 持续 100 RPM、Nova 只读监视](motor_m4_continuous_test.md)，M2 保持禁能。本页为此前 M2 持续运行固件的操作，不适用于当前工程烧录结果。

本页历史模式：`MOTOR_CONTINUOUS_TEST=1`、`SelectedPort=2`。上电/RESET 约 2 秒，且停稳检查通过后，
M2 自动以 100 RPM 目标持续正转，没有定时终止，也不自动反转。M1/M3/M4 每周期保持禁能。

## 参数与保护

| 项目 | 设置 |
| --- | --- |
| M2 正转 Kp / Ki / Kd | 0.001329 / 0.010857 / 0 |
| M1 正转参数（保留、禁能） | 0.000957 / 0.009191 / 0 |
| M3 正转参数（保留、禁能） | 0.001176 / 0.013926 / 0 |
| PWM / 积分限幅 | 0.69 / 0.69 |
| 编码器 | 30:1，11 线四倍频，1320 CPR |
| 测速滤波 / 控制周期 / 遥测周期 | 0.03 s / 10 ms / 20 ms |

依据见 [M2 八轮调参记录](motor_pid_20261011_m2/README.md)。
原驱动、方向、无运动、超速、反馈/驱动失败、命令超时、50 ms 调度间断、停稳互锁及故障锁定不变。
故障时四轮禁能，不自动重启。持续模式由本地更新目标，不要求远程心跳；AI 模式的通信保护代码保留。

## 烧录与查看曲线

1. 关闭 Nova 串口和旧桥接。断电后确认左后轮电机线及编码器线接 M2、电驱已安装，固定车体并架空车轮。
   M2 PWM 为 PC8/PC9（TIM8 CH3/CH4），编码器为 PA15/PB3（TIM2 CH1/CH2），沿用已验证方向。
2. DAPLink 烧录 `MDK-ARM/m2_continuous_100rpm_tuned_nova_monitor.hex`，运行并按 RESET。
   约 2 秒后 M2 自动运行，无需 Nova 命令。RESET 会重新开始测试。
3. 双击 `tools/ble_bridge/Restart-Bridge.cmd`，按提示等待 READY，Nova 打开 COM12、115200、8N1、无流控。
   COM11 留给桥接，COM5 为 DAPLink。
4. 字符串、CRLF 发送一次 `LOOP:2`，应回复 `OK:LOOP:2`；再发送 `PID?`，
   应回复 `PID:0.001329,0.010857,0.000000`。
5. PID 页面选择 CH2，取消“暂停显示”。目标线为 100 RPM，实际线为编码器测得的滤波转速。
   可勾选 Output，稳定后采集至少 10 秒并导出 CSV。固件拒绝 PID/目标修改和 AI 试验，返回 `monitor_only`。

断开蓝牙、关闭 Nova 或遥测拥塞不会停止本地持续运行。结束时关闭电机电源；
通信正常时也可字符串、CRLF 发送 `@{"cmd":"stop","id":"monitor-stop","channel":2}`，
立即禁能并锁定，RESET 后才会重新运行。启动延时期间收到 STOP 也会锁定。

## 异常诊断与模式恢复

停止后保留现场，查看 `MotorSafety.status_.fault`、`MotorSafety.status_.failed_port`、
`MotorBench.status_.phase`、`MotorBench.status_.failure`、`MotorBench.status_.duty`。
M2 端口为 2，NoMotion=6 表示未检测到有效转速；不通过取消保护继续试跑。

以后恢复 M2 AI 调参需设 `MOTOR_CONTINUOUS_TEST=0`、`NOVA_MOTOR_TUNING=1`、`NOVA_TUNING_PORT=2`，
重新编译，并更新档案基准为新 PID 0.001329/0.010857/0，避免旧档案覆盖固化结果。
