# 八路灰度测试记录

用户已确认八路灰度 OLED 测试正常。按用户要求，固件中的灰度诊断入口、显示对象和测试宏已移除。
灰度与 OLED 驱动保留；GrayOledTest 仅位于 tests/frame/fixtures，继续用于 256 种 GPIO 组合的主机回归。

已确认接线：x1～x8 分别为 PC0、PC1、PC2、PC3、PA4、PA5、PB0、PB1。
OLED 为 I2C1 PB6/PB7、0x3C、SSD1306 128×32。
此前界面分 X1-4 / X5-8 显示原始 0/1，并以 FLIP 记录预热后是否观察到两种电平。
FLIP 不代表校准成功或断线检测。该诊断不参与当前 Keil 构建，也不存在启用它的测试宏。

保留测试：Run-Gray8LpTests.ps1、Run-GrayOledTests.ps1。
清理版 V6 构建为 build-clean-v6.log，通过 0 Error / 0 Warning。
需下载新固件，开发板才会停止运行已烧录的旧测试。
