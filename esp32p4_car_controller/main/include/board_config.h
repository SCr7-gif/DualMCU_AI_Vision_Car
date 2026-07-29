#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/uart.h"

/*
 * ESP32-P4 WIFI PICO ETH 板级引脚表。
 *
 * 重要约束：
 * - GPIO7~13 已分配给 I2C / ES8311 I2S，不要挪作其他用途。
 * - GPIO53 已分配给功放 PA_CTRL，不要挪作其他用途。
 * - OV5647 的 MIPI CSI D0/D1/CLK 是 ESP32-P4 专用硬件信号，
 *   不能在软件里按普通 GPIO 配置。
 */

/* I2C：ES8311 Codec + OV5647 配置总线 */
#define BOARD_I2C_PORT              I2C_NUM_0
#define BOARD_I2C_SDA_GPIO          GPIO_NUM_7
#define BOARD_I2C_SCL_GPIO          GPIO_NUM_8
#define BOARD_I2C_FREQ_HZ           400000

/* ES8311 常见 7-bit I2C 地址，原理图未绑地址脚时通常为 0x18 */
#define BOARD_ES8311_I2C_ADDR       0x18

/* ES8311 I2S / MCLK */
#define BOARD_ES8311_MCLK_GPIO      GPIO_NUM_13
#define BOARD_ES8311_BCLK_GPIO      GPIO_NUM_12
#define BOARD_ES8311_LRCK_GPIO      GPIO_NUM_10
#define BOARD_ES8311_DOUT_GPIO      GPIO_NUM_11  /* ES8311 ASDOUT -> ESP 录音输入 */
#define BOARD_ES8311_DIN_GPIO       GPIO_NUM_9   /* ESP 播放输出 -> ES8311 DSDIN */
#define BOARD_ES8311_PA_CTRL_GPIO   GPIO_NUM_53

/*
 * Qwen Realtime 输出音频是 PCM / 24000 Hz / 16-bit / mono。
 * ES8311 这类 Codec 在本板上用标准 I2S stereo slot 更稳，播放前把 mono 复制到 L/R 两个 slot。
 */
#define BOARD_AUDIO_SAMPLE_RATE_HZ  24000
#define BOARD_AUDIO_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_16BIT
#define BOARD_AUDIO_SLOT_MODE       I2S_SLOT_MODE_STEREO
#define BOARD_AUDIO_CHANNEL_COUNT   2

/* STM32 通信 UART */
#define BOARD_STM32_UART_NUM        UART_NUM_1
#define BOARD_STM32_UART_TX_GPIO    GPIO_NUM_47  /* ESP TX -> STM32 PA10 */
#define BOARD_STM32_UART_RX_GPIO    GPIO_NUM_48  /* ESP RX <- STM32 PA9 */
#define BOARD_STM32_UART_BAUDRATE   115200

/* Waveshare ESP32-P4-ETH RMII IP101 可选网络后端 */
#define BOARD_ETH_MDC_GPIO          GPIO_NUM_31
#define BOARD_ETH_MDIO_GPIO         GPIO_NUM_52
#define BOARD_ETH_PHY_POWER_GPIO    GPIO_NUM_51
#define BOARD_ETH_RMII_CLK_GPIO     GPIO_NUM_50
#define BOARD_ETH_PHY_ADDR          1

/*
 * Waveshare ESP32-P4-WIFI6-KIT 联网硬件。
 *
 * 你给的原理图显示外置联网芯片是 ESP32-C6-MINI-1：
 * - ESP32-P4 GPIO6 通过 0R 接 C6_IO2。
 * - ESP32-P4 GPIO54 通过 0R 接 C6_CHIP_PU。
 * - C6_U0TXD / C6_U0RXD 引到 H4 调试排针。
 * - H4 的 C6_IO9 是 ESP32-C6 下载启动脚：烧录 C6 时可以拉低，
 *   但正常运行 hosted slave 固件时必须释放/拉高。若 IO9 被串口小板
 *   或跳线拉低，C6 会一直停在 ROM "waiting for download"，P4 将只能
 *   看到 SDIO card init success，随后 hosted ready 超时。
 *
 * 这只能说明板上有 C6 协处理器和部分控制脚；真正让 P4 联网还需要
 * Waveshare 或 Espressif 的 hosted/remote Wi-Fi 驱动来定义 P4 与 C6
 * 的数据通道。当前不要把这些脚挪作 STM32 或相机用途。
 */
#define BOARD_C6_IO2_GPIO           GPIO_NUM_6
#define BOARD_C6_CHIP_PU_GPIO       GPIO_NUM_54

/* OV5647：I2C 地址与控制脚。MIPI CSI 数据脚不在这里配置。 */
#define BOARD_OV5647_I2C_ADDR       0x36
#define BOARD_OV5647_RESET_GPIO     GPIO_NUM_NC
#define BOARD_OV5647_PWDN_GPIO      GPIO_NUM_NC

/*
 * ESP32-P4-WIFI6-KIT 摄像头座信息：
 * - I2C 使用 ESP_I2C_SDA/SCL，也就是 GPIO7/GPIO8。
 * - MIPI CSI 是 2-lane：CSI_D0_P/N、CSI_D1_P/N、CSI_CLK_P/N。
 * - 原理图还有 CSI_IO0/CSI_IO1，其中 CSI_IO0 默认 10K 上拉，
 *   CSI_IO1 当前 NC。它们具体是否接 OV5647 RESET/PWDN 要看摄像头
 *   小板定义，不能在没有确认前当普通 GPIO 拉动。
 */

/* STM32 测试协议 */
#define BOARD_STM32_FRAME_HEAD0     0xAA
#define BOARD_STM32_FRAME_HEAD1     0x55
