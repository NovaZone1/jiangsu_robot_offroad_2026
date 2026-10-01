> 当前状态：已按用户要求撤下超声波＋OLED临时测试。MainFrame已恢复原入口，Keil中5个测试相关源文件条目已移除。下文接入步骤仅为历史说明，不代表当前已启用。
> 专用RangeDisplayApp已从FrameComponets移出，仅保留在tests/frame/fixtures供主机回归测试使用。可复用驱动保留，未参与当前固件编译。不要重新应用旧接入补丁；不要用Sync-FrameProject将暂存驱动重新加入工程。用户的Keil版本及RTE设置保留。

# 超声波测距 + OLED 临时测试

用户已批准接入，MainFrame中的测试代码以 `TEMP_RANGE_OLED_TEST` 标记。
目前只绑定原厂超声波并注册显示应用，电机和灰度原状态不变。
不需要完成整车自检即可看到测距结果；没有新增自动驾驶或电机输出。

## 上板步骤

1. Keil打开 `MDK-ARM/jiangsu_robot_offroad_2026.uvprojx`，使用ARM Compiler V6完整Rebuild。
2. 确认0 Error / 0 Warning，按原方式下载、复位，保持车辆静止。
3. OLED应显示ULTRASONIC、厘米值和OK。初始化/无回波时显示横杠及相应状态。
4. 将挡板放在10、20、50、100 cm，检查距离变化；移开挡板检查TIMEOUT及恢复。
5. 验证刷屏不影响回波：PF11约20 us触发、至少60 ms间隔；OLED用PB6/PB7的I2C1。

详细状态、故障检查和测试覆盖见 `oled_factory.md` 与 `ultrasonic_factory.md`。
尚无实车验证结果；已知原RTOS周期配置问题未在本次修改范围内修复。

本次接入检查：Keil XML解析通过，新增源文件引用均存在且无重复条目；
OLED联动测试重新使用MSVC编译运行通过，先前构建的超声波两组测试和原框架测试复跑通过。
git diff --check及反向补丁检查通过。当前环境未发现可用Keil编译入口，未执行V6完整构建。

## 测试结束后移除

先关闭Keil工程。在没有再次修改这两处接入代码的情况下，可以只反向移除本次接入：

```powershell
git apply --reverse --check docs/range_oled.integration.patch
git apply --reverse docs/range_oled.integration.patch
```

补丁只涉及 `FrameComponets/Apps/Src/MainFrame.cpp` 和 `.uvprojx`，
不会删除其他代码。若检查不通过，先检查后续改动，不要强制覆盖文件。
反向移除后完整Rebuild并重新烧录；只修改电脑源码不会停止板上旧固件。

新增驱动文件可以先保留供复用，但不要再运行Sync-FrameProject将它们重新加入工程。
若用户要求彻底清理，再按下面清单核对调用方并删除对应新增文件：

- `FrameComponets/Apps/Inc/RangeDisplayApp.hpp`、`Apps/Src/RangeDisplayApp.cpp`
- `FrameComponets/Bsps/Inc/bsp_ultrasonic_echo.h`、`Bsps/Src/bsp_ultrasonic_echo.c`
- `FrameComponets/Bsps/Inc/bsp_oled_bus.h`、`Bsps/Src/bsp_oled_bus.c`
- `FrameComponets/Mods/Inc/ultrasonic_factory.hpp`、`Mods/Src/ultrasonic_factory.cpp`
- `FrameComponets/Mods/Inc/oled_factory.hpp`、`Mods/Src/oled_factory.cpp`
- `tests/frame/Run-UltrasonicTests.ps1`、`Run-OledTests.ps1`
- `tests/frame/ultrasonic_factory_tests.cpp`、`ultrasonic_echo_tests.cpp`、`oled_factory_tests.cpp`
- `tests/frame/ultrasonic_mocks/`、`oled_mocks/`
- 本次新增的超声波/OLED文档与接入补丁

清单中的简写 Apps/Bsps/Mods 路径均位于 FrameComponets 下。
原有 `ultrasonic.hpp/.cpp`、主机frame_tests、原GPIO BSP及其他稳定文件不得删除。
