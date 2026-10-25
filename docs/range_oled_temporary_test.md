# 超声波 OLED 测试记录

临时超声波距离显示入口已按用户要求从固件清理。
原厂超声波驱动保留并在 MainFrame 绑定，OLED 驱动保留。
RangeDisplayApp 仅位于 tests/frame/fixtures，用于主机联动回归，不参与 Keil 构建。

资源：PF11/TRIG、PF12/ECHO、TIM7、EXTI12、DWT；OLED 为 PB6/PB7、I2C1、0x3C、128×32。
驱动保留 20 us 触发、至少 60 ms 间隔、35 ms 回波超时、200 ms 数据有效期与无效样本清除。
公开厘米接口仍为 FactoryUltrasonic::ReadDistanceCm，原 Ultrasonic.Sample 保持毫米。
此前显示以厘米一位小数呈现距离，无回波显示横杠和 TIMEOUT；该界面现已撤下。

主机 Run-UltrasonicTests.ps1 / Run-OledTests.ps1 保留，回归覆盖驱动和联动显示故障恢复。
旧接入补丁已移除，不使用反向补丁恢复入口。无固件测试宏；旧测距 HEX 已从本地清理。
需下载当前正式固件才能结束板上旧测试，清理版 V6 构建通过 0 Error / 0 Warning。
