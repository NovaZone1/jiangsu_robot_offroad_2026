#pragma once

/* Board handles/pins are supplied by Apps/MainFrame after CubeMX setup. */
#define FRAME_MAX_GPIO_INSTANCES 48U
#define FRAME_MAX_UART_INSTANCES 4U
#define FRAME_MAX_APPLICATIONS 8U
#define FRAME_GRAY_MAX_CHANNELS 16U
#define FRAME_CONTROL_PERIOD_MS 1U
#define FRAME_SYSTEM_PERIOD_MS 5U
#define FRAME_REQUIRED_LAPS 3U
#define FRAME_MAX_MOTORS 8U

// 当前固件执行一次四轮速度测试；恢复比赛调度时改为 0 后重新编译。
#define FRAME_MOTOR_TEST_ENABLED 1U
