# DualMCU AI Vision Car

基于 ESP32-P4 与 STM32F103 的双 MCU 多模态 AI 视觉小车。

ESP32-P4 作为联网与 AI 决策层，负责 OV5647 图像采集与 JPEG 编码、ES8311/I2S 音频链路、Web 调试界面，以及通过 WebSocket 接入 Qwen Realtime。STM32F103 作为实时执行与安全层，负责电机、云台、超声波、DHT11、GPS 和 WS2812 等外设。

## 技术栈

- C、FreeRTOS
- ESP-IDF、STM32 HAL
- UART、DMA、I2C、I2S、PWM、MIPI CSI
- HTTP/MJPEG、WebSocket、Qwen Function Calling
- 双 MCU 分层控制、非阻塞行为状态机、双层安全保护

## 核心能力

- 语音与图像多模态交互
- 网页实时画面、状态监控与远程控制
- 自主巡逻、视觉辅助避障和云台扫描
- ESP32-P4 与 STM32 自定义 UART 二进制协议
- 速度/舵机限幅、通信超时停车、超声波近距离制动
- PSRAM 缓冲池与 FreeRTOS Ring Buffer 流式音频解耦

## 仓库结构

```text
esp32p4_car_controller/    ESP32-P4 AI、联网、音视频与行为调度
stm32_motion_controller/  STM32F103 实时控制、外设驱动与安全逻辑
docs/                     架构、协议、测试计划与调试记录
```

## 构建

ESP32-P4 工程使用 ESP-IDF：

```powershell
cd esp32p4_car_controller
idf.py set-target esp32p4
idf.py build
```

STM32 工程可使用 STM32CubeMX/Keil 工程文件，或运行：

```powershell
cd stm32_motion_controller
.\tools\build_stm32_gcc.ps1
```

真实 Wi-Fi 密码和 Qwen API Key 通过本地 NVS 文件配置，不应提交到仓库。示例配置位于 `esp32p4_car_controller/config/`。
