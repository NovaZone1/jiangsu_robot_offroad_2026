# DX-BT24 BLE → Nova 串口桥接

## 自己双击启动 / 复位后重连

- 首次启动且没有旧桥接：双击 `Start-Bridge.cmd`。
- RESET、烧录、断电后，或不确定旧桥接是否仍在：先在 Nova 关闭 COM12，
  双击 `Restart-Bridge.cmd`，按提示按任意键。脚本请求旧桥接退出，等待 25 秒释放
  BLE 会话，再在可见窗口启动新桥接。只有看到 `READY` 才表示连接成功。
- 看到 `READY` 后保持窗口打开，Nova 重新连接 COM12（115200、8N1、无流控），
  字符串 + CRLF 发送一次 `LOOP:1`，收到 `OK:LOOP:1` 后进入 PID 页 CH1 看曲线。
- 结束桥接可按 Ctrl+C 或双击 `Stop-Bridge.cmd`。不要反复双击并行开启多个窗口。
- 如果仍提示 `Device not advertising`，关闭占用模块的手机 APP，再按上述重连流程操作。

这些脚本不更改固件、PID 或保护，也不发送启动电机命令。当前四轮 PID 清理版上电禁能，
电机启动由正式应用管理。桥接关闭本身不发送停车命令，不能作为停止电机的手段。
重连脚本只按本次双击请求重建一次连接，掉线后不会自动重连或重放旧命令。

## 已确认的硬件

2026-10-11 实际扫描连接用户的 `YahBoom_BL`，模块照片为 **DX-BT24 / BLE 5.1**。
实际读取到服务 `0000ffe0-0000-1000-8000-00805f9b34fb`：

- FFE1：read / write / write-without-response / notify，作为设备到电脑的通知通道。
- FFE2：write / write-without-response，作为电脑到设备的写入通道。
- 本次连接协商 MTU 为 256；桥接默认仍用 20 字节分包、包间 5 ms，优先使用有响应写入。

直接 BLE 探测已收到固件 `hello`、`get` 的成功 ACK，以及真实四轮遥测。
主分支四轮默认 Kp=Ki=Kd=0；连接桥接不修改参数、不启动电机。
这确认了 BLE、模块 UART、STM32 与 Nova 协议之间的双向链路；未发送 PID 修改或电机启动指令。
完整虚拟串口链路也已实测：从 COM12 发送 LOOP:1、PID? 和带唯一 ID 的 v2 get，
经 COM11/BLE 收到对应的 OK、参数读回和成功 ACK，同时接收到真实遥测；测试后释放 COM12。

## 本机使用：Nova 选 COM12

```text
Nova COM12 ↔ com0com 虚拟串口对 ↔ 桥接 COM11 ↔ 电脑 BLE ↔ DX-BT24 ↔ STM32 USART1 DMA
```

COM11/COM12 已实测是双向互通、且能被 Nova 枚举的虚拟串口对。
旧 COM20/COM21 虽可直接打开，但未出现在本机 pyserial/Nova 端口列表，因此改用 COM11/COM12。**COM5 是 DAPLink，不在此链路中。**
本工具只接受已识别的 com0com 虚拟端口，避免把数据误发到硬件串口。

1. 给小车/蓝牙正常供电，电脑打开蓝牙。关闭占用模块的手机 APP；不要在 Windows 配对页面反复连接。
   本工具直接建立 BLE GATT 连接，不需要 Windows 生成蓝牙 COM 口。
2. 双击 **Start-Bridge.cmd**，窗口出现 `READY` 后保持窗口打开。
   如果后台桥接已经启动，不要再启动第二个；COM11 被占用的提示通常表示已有程序运行。
3. Nova 串口页选择 **COM12，115200，8N1，无流控**，点击连接。
4. 发送方式选**文本**，结尾选 **CRLF**，发送 `LOOP:1`。
   收到 `OK:LOOP:1` 后，再发 `PID?`，应收到 `PID:0.000000,0.000000,0.000000`（未修改参数时）。
5. 第一次收到 Nova 发来的字节后，桥接开始转发遥测，避免 Nova 未打开时把虚拟串口缓冲区塞满。
   如果刚连接后接收区空白，先执行第 4 步。选择 PID 页的通道 1～4 可观察四轮曲线。
6. 用桥接窗口的 Ctrl+C 或双击 **Stop-Bridge.cmd** 停止。后台模式也可用 Stop-Bridge.cmd 停止。

Nova 应使用先前核对的、支持 `@JSON` 遥测的版本。桥接不修改任何 Nova 源码。
看到发送记录还不能证明通信成功；必须看到设备返回的 `OK:LOOP:1` 或匹配请求 ID 的 ACK。

如果断电、BLE 断线、队列溢出或串口写入不完整，桥接结束当前会话并向 Nova 尝试报告
`ble_bridge_closed` 故障，不自动重连、不重发旧的电机命令。
重连时先关闭 Nova 的 COM12，再重新启动桥接，看到 READY 后重新打开 COM12。
关闭/重新打开 Nova 后也按此流程重新建立会话，避免使用上一会话的串口缓冲数据。

## 安装与命令行

本机已安装独立 `.venv`，不用修改系统 Python，也不用再次安装 com0com。
换电脑时需先安装 Python 3.11 或更新版本，并通过正规安装流程建立一对 com0com 虚拟串口。
虚拟串口端口号可以不同；桥接占用一端，Nova 打开另一端，二者不能相同。

从本目录运行：

```powershell
.\Bridge.ps1 -Action Setup -Python 'D:\Environment\Python\Python314\python.exe'
.\Bridge.ps1 -Action Scan
.\Bridge.ps1 -Action Inspect
.\Bridge.ps1 -Action Probe      # 只读 hello/get，验证设备链路
.\Bridge.ps1 -Action Bridge -Port COM11
.\Bridge.ps1 -Action Bridge -Port COM11 -Background
.\Bridge.ps1 -Action Stop
```

扫描同名设备多于一个时程序拒绝猜测，可在上述命令添加 `-Address '实际扫描到的地址'`。
默认目标名 `YahBoom_BL`，名称前后空格会被忽略；其他名称用 Python 的 `--name` 参数。
后台模式输出日志位于 `runtime/bridge-*.err.log`，查看 READY、收发字节统计和错误原因。
`.venv`、runtime 和缓存均被 Git 忽略；不提交本机运行日志或依赖副本。

## 协议与控制边界

桥接原样传输字节，不更改 PID 参数、遥测时间戳、序号或保护命令，不伪造电机数据。
未激活会话时丢弃的是尚未向 Nova 转发的启动遥测；激活后禁止静默丢字节。
BLE 接收队列最多 128 个通知块，串口写入和 BLE 写入都有超时；发生异常停止会话。
一次只允许一个 BLE 连接和一个 COM11 所有者；没有后台自动重连。
软件发给 Nova 的断链故障帧不等同于单片机已经停车，设备端原有保护必须继续保留。

主分支默认关闭自动试验；个人试验档案仅保存在车辆分支。显式启用单轮调参后，
固件仍独立执行心跳超时、试验限时、停稳互锁和原电机保护。
桥接本身只传输字节，不生成心跳、不自动使能、不自动重连，旧固件仍需重新烧录才能试转。

## 测试

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s . -p test_bridge.py -v
```

覆盖 BLE/串口分片双向保序、MTU 分包、通知队列满、部分写入失败、断线退出、
同名设备拒绝猜测、服务校验和读回探测的行恢复。蓝牙实物收发另通过 Probe 验证。

参考：[Bleak 官方客户端 API](https://bleak.readthedocs.io/en/latest/api/client.html)。
