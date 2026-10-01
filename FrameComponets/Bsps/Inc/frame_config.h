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

// 灰度 OLED 临时试验也已撤下；保留配置名，试验源码仅用于主机回归。
#define FRAME_GRAY_OLED_TEST_ENABLED 0U
#if FRAME_GRAY_OLED_TEST_ENABLED
#error "Bench tests are no longer firmware entry points"
#endif
