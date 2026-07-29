# ESP32-P4 剩余硬件信息清单

ESP32-P4 代码层已经有 STM32 链路、Qwen 工具、音频读写、NVS 密钥、Web 调试、串口 monitor 控制、图像上传管线和可选 V4L2 相机取帧后端。

2026-06-29 用户补充：板卡是 Waveshare ESP32-P4-WIFI6-KIT。原理图显示联网协处理器是 `ESP32-C6-MINI-1`，不是 ESP32-C4。工程已新增 `ROBOT_NETWORK_WAVESHARE_P4_WIFI6_KIT` 后端，用于拉起 C6 `CHIP_PU`，调用 `esp_hosted_init()`，并通过 `esp_wifi_remote` 的标准 Wi-Fi STA 流程联网。

剩下不能凭空完成的部分是板卡级驱动配置。

## 1. 网络硬件

ESP32-P4 本体没有原生 Wi-Fi。现在已经确认板上有外置 ESP32-C6，并已按 Waveshare 官方文档接入 `esp_wifi_remote` 与 `esp_hosted` 组件。若仍不能联网，需要继续确认：

- C6 是否已经烧录匹配的 hosted 固件。
- 当前 ESP-IDF 组件版本是否和 Waveshare 示例一致。
- P4 和 C6 的 SDIO/hosted 配置是否需要额外 sdkconfig。

已从原理图确认：

- P4 GPIO6 通过 0R 接 `C6_IO2`。
- P4 GPIO54 通过 0R 接 `C6_CHIP_PU`。
- C6 `U0TXD/U0RXD` 引到 H4 调试排针。

仍可能缺：

- C6 侧 hosted 固件烧录步骤。
- Waveshare 示例中的额外 sdkconfig 选项。

## 2. OV5647 MIPI CSI

当前工程已经能 I2C probe OV5647，并且 `image_sampler` 已经能上传 JPEG 给 Qwen。

要让 `/dev/video0` 真正输出 JPEG，需要确认：

- OV5647 MIPI CSI lane 数量：原理图已确认 2-lane。
- MIPI 时钟和 lane 顺序：原理图已确认 `D0/D1/CLK` 差分线。
- XCLK 是否由 ESP32-P4 输出，频率是多少。
- RESET/PWDN 是否接 `CSI_IO0/CSI_IO1`；如果接了，需要确认摄像头小板定义。
- 使用的 `esp_video` board 配置或官方例程是哪一个。
- 目标输出格式是否支持 `V4L2_PIX_FMT_JPEG`。

## 3. 当前可以直接测试的内容

不用等待网络和相机驱动，也可以先测：

```text
status
net
stop
led cute
gimbal_action scan_slow
behavior cute
move forward 25 600
capture
```

其中 `capture` 现在可以测上层触发链路；打开 V4L2 后端并完成板卡相机初始化后，它才会真正拿到 JPEG。

## 4. 下一步接入顺序

建议先接网络，因为 Qwen Realtime 必须联网：

1. 使用当前 `esp_hosted` 后端构建烧录，观察是否拿到 IP。
2. 如果 `esp_hosted_init` 或 Wi-Fi 连接失败，再对照 Waveshare 官方示例补 C6 固件和 sdkconfig。
3. 确认 Qwen 出现 `connected/session updated`。
4. 接 `esp_video` OV5647 MIPI CSI 初始化。
5. 打开 `CONFIG_ROBOT_CAMERA_V4L2_CAPTURE`。
6. 测 `capture` 是否出现 `image upload reason=1 len=xxxxx ret=ESP_OK`。
