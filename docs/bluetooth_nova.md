# USART1 蓝牙与 Nova 接入

> 更新：用户实际模块已确认是 DX-BT24 BLE 5.1，已扫描并完成 BLE 双向参数查询。
> 本机通过 [BLE 桥接程序](../tools/ble_bridge/README.md) 接入；桥接用 COM11，Nova 选择 COM12。
> 不需要在 Windows 配对页面创建蓝牙串口，也不使用 DAPLink 的 COM5。

## 当前可以做什么

已接入普通串口透传蓝牙，**115200、8 数据位、无校验、1 停止位，无流控**。
支持 Nova 普通 PID 页的整组/单参数下发、参数读回，以及新版 Nova v2 的带时间戳曲线、通道、ACK 和停车。
上电不试转，不因连接或修改 PID 而启动电机。原电机保护保留；调参模式不运行比赛应用。

当前默认固件已补齐 **M1 单轮 AI 调参**：只有通道 1 开放 configure / heartbeat / set_target，
使用 D-on-measurement、10 ms 控制周期，并保留电机保护和停稳互锁。
M2～M4 持续禁能，比赛应用暂停；循迹自动调参尚未开放。
完整步骤和限幅见 [M1 调参说明](nova_m1_tuning.md)，导入 [520 M1 档案](nova_m1_520_profile.json)。
`NOVA_M1_TUNING=0` 时恢复原比赛调度与 D-on-error 参数接口，拒绝远程试转。

参考的是 Nova `main` 提交
[`a0de0488e37597d41cea9bde2aa15752a418261c`](https://github.com/zwj051029/Nova/tree/a0de0488e37597d41cea9bde2aa15752a418261c)，
核对了 `tuning/protocol.py`、`tuning/automatic.py`、`pid_page.py` 与 README，未修改上位机仓库。

## 接线与硬件资源

| STM32 | 蓝牙模块 | 用途 |
| --- | --- | --- |
| PA9 / USART1 TX | RXD | 单片机发送 |
| PA10 / USART1 RX | TXD | 单片机接收 |
| GND | GND | 共地 |
| 合适的电源 | VCC | 接口图标出 3V3，模块供电要求以其实际规格为准 |

截图中 U6 的 3 脚经 1 kΩ 接到 RST，**不要把这个脚当作 GND**；共地应从板上明确标注的 GND 引出。
PA9/PA10 也连接板载 CH340，外部蓝牙 TX 与 CH340 TX 不能同时驱动 PA10。
使用 DAPLink 下载时确认板载串口一侧已按硬件设计隔离，或使用已经确认无冲突的连接方式。
驱动不自动发送 AT 命令、不操作模块 RST，不假定配对密码或连接状态引脚。

USART1 无重映射；DMA1 Channel 4 发送，Channel 5 循环接收。
`bsp_uart1_dma.c` 独占 USART1、这两个 DMA 通道及三个 IRQ，优先级均为 7；不调用 RTOS API。
这是独立的寄存器级 BSP，不重复定义 HAL UART 回调，不影响原 `bsp_uart` 给其他串口使用。
不得再给 USART1 调用 MX_USART1_UART_Init、BspUart_InstRegister 或另一个 DMA 初始化。
`.ioc` 只补充 BSP 资源说明，外设仍由 BSP 初始化；后续 CubeMX 重新生成必须保留此所有权约定。

## 文件与调度

| 层 | 文件 | 职责 |
| --- | --- | --- |
| Bsps | `bsp_uart1_dma.h/.c` | USART1、循环 RX DMA、发送队列、IRQ 与错误恢复 |
| Mods | `bluetooth.hpp/.cpp` | 通用串口透传接口，默认波特率集中可改 |
| Mods | `nova_protocol.hpp/.cpp` | 有界行解析、旧 PID / v2、ACK、遥测、故障帧 |
| Apps | `NovaLink.hpp/.cpp` | 通道映射、真实 PID 读写、参数范围与曲线采集 |

MainFrame 初始化，FrameTickCpp 每 1 ms 调用 NovaLink.Service，故障锁定时仍可读状态和停车。
DMA 负责字节搬运：RX 256 字节循环区、512 字节软件队列；TX 四个 384 字节槽位，发送前复制。
DMA 半满/全满中断搬运增量；任务每毫秒查询 NDTR，接收短帧不需要等待填满，也无逐字节 RX 中断。
解析每周期最多 64 字节，缓冲满时丢整帧/重同步，不忙等发送。
RX 溢出、UART/DMA 错误会作废半行，丢到下一换行再恢复；TX 出错会补换行，避免拼接坏帧。
中断/轮询必须及时运行：115200 时半个 RX DMA 区约 11.1 ms 填满；不能长时间屏蔽中断。
这些设计减少串口搬运开销，实际 CPU 占用和蓝牙延迟仍需在板上测量。

NovaProtocol 不依赖 HAL，所有 Execute 回调在框架任务执行。
v2 单行最多 383 字节，ID 为 1～47 个字母/数字/下划线/连字符，支持空白和科学计数数字；
拒绝重复字段、未知字段、非有限值、缺失字段和超长行。
普通命令缓存最近四条 v2 请求的 ACK；相同 ID/内容重发只重发 ACK，内容不同拒绝。
心跳允许省略 type，并在每次接收时刷新租期，不参与去重；STOP 每次执行且不清除故障。
ID 应在会话内保持唯一；这个有界重发缓存不是跨重启、跨会话的永久事务数据库。

## 在 Nova 中使用

1. 用 DAPLink 下载当前工程，复位。电脑连接蓝牙对应的串口，Nova 选择 115200、8N1。
   Nova 使用普通串口，模块/电脑需能提供串口或等效桥接；仅提供 BLE GATT 的模块不能直接当 COM 口。
2. 使用支持 v2 遥测的上述版本 Nova。M1 调参模式每 20 ms 只上传通道 1（50 Hz）；比赛模式仍轮询五通道，每通道约 10 Hz。
   DMA 入队失败会丢整帧，序号仍递增，便于发现丢帧；不会阻塞电机控制。
3. 参数修改前停车、禁能。在串口页以文本发送 `LOOP:1`，勾选追加换行，看到 `OK:LOOP:1` 后，
   再到 PID 页发送参数。普通 PID 指令本身没有通道号；**仅在图表选择曲线不会切换下发通道**。
4. 可发送 `PID?` 读回实际参数。成功返回 `OK:PID`，失败返回 `ERR:原因`，失败不部分更新。
   参数只在 RAM 生效，复位恢复 MotorSpeedProfiles / Track 默认配置；不写 Flash。

| 通道 / LOOP | 对象 | 目标 SP | 实际 PV | 输出 OUT |
| --- | --- | --- | --- | --- |
| 1 | M1 左前 | 输出轴 RPM | 闭环使用的滤波 RPM | 有符号 PWM 占空比 |
| 2 | M2 左后 | 同上 | 同上 | 同上 |
| 3 | M3 右前 | 同上 | 同上 | 同上 |
| 4 | M4 右后 | 同上 | 同上 | 同上 |
| 5 | 循迹 | 0 | `-line_error`，归一化 | 左右轮差速 RPM |

循迹上传采用 `SP-PV=line_error`，与当前控制器转向符号一致。
只有真实、有效、正在 Following 的新样本才上传通道 5，停车时不会伪造零误差。
电机无反馈或保护故障返回 fault 帧；时间戳来自真实采样时刻，同一旧样本不重复上传。
静止时编码器仍采样，四路可以显示实际的零转速。

普通指令（每条末尾发送 CRLF 或 LF；代码框中的换行即分帧）：

```text
LOOP:1
PID?
PID:0.001200,0.008000,0.000000
PID:Kp=0.001200
PID:Ki=0.008000
PID:Kd=0.000000
```

v2 可在串口页直接发送，每个新请求使用不同 ID：

```text
@{"type":"command","id":"manual-1","cmd":"hello","channel":1}
@{"type":"command","id":"manual-2","cmd":"get","channel":1}
@{"type":"command","id":"manual-3","cmd":"set_pid","channel":1,"gains":[0.0012,0.008,0]}
@{"type":"command","id":"manual-4","cmd":"stop","channel":1}
```

stop 会停车并禁能全部四轮，不清除故障锁定；它不会释放滑行互锁或恢复比赛启动请求。
configure、heartbeat、set_target 只对 M1 调参模式的通道 1 开放；其他通道拒绝。

电机调参入口范围为 Kp 0～0.1、Ki 0～1、Kd 0～0.01；循迹为 Kp/Ki 0～100、Kd 0～10。
范围只是拒绝明显异常输入，不代表范围内任意参数都有良好控制效果。
循迹增加可配置 Ki，默认仍为 0；积分输出限幅默认 6 RPM，差速限幅及丢线处理保留。
电机参数更新保留滤波和超时；M1 的 configure 可收紧输出/积分上限，不更改 1320 CPR、不自动 Enable。

## 验证与排查

```powershell
.\tests\frame\Run-Tests.ps1
.\tests\frame\Run-NovaTests.ps1  # 单独运行 DMA、协议及实际应用绑定测试
```

有独立 g++/gcc 时用 `-Compiler` / `-CCompiler` 指定；Keil V6 完整 Rebuild 生成当前 AXF/HEX。
已有电机、灰度、循迹回归保持通过。新增覆盖拆包/粘包、DMA 回绕、错误恢复、队列满、命令拒绝、
通道隔离、停车禁能、PID 参数读回、循迹积分限幅，以及 STOP 不清除系统/电机故障。
主机测试与编译不代表蓝牙已经实物联通。本次未下载固件或运行电机。

Keil 可查看 `NovaLink.GetBluetooth().GetStats()` 和 `NovaLink.GetProtocol().GetStats()` 对应的成员，
或在任务中把快照存入 Watch 变量；不要通过调试器在 ISR 中调用这些方法。
`IsReady()` 仅表示 UART 已初始化，不代表蓝牙已配对。
收不到数据先检查共地、交叉 TX/RX、CH340 冲突、蓝牙串口状态和 115200 是否一致。

如需核对新版 Nova 是否仍兼容，先编译主机测试，然后执行：

```powershell
python .\tests\frame\Check-NovaCompatibility.py --nova-root E:\RobotCompetition\Resource\other_code\Nova
```
