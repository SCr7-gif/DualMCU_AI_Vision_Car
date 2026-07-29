# ESP32-P4 小闭环 7：Waveshare ESP32-P4-WIFI6-KIT 网络诊断

## 1. 目标

确认工程按 ESP32-P4-WIFI6-KIT 运行，拉起外置 ESP32-C6 的 `CHIP_PU`，通过 `esp_hosted` 初始化 P4<->C6 通道，并尝试用 NVS Wi-Fi 配置联网。

## 2. 原理图信息

- 联网协处理器：`ESP32-C6-MINI-1`。
- P4 GPIO6 通过 0R 接 `C6_IO2`。
- P4 GPIO54 通过 0R 接 `C6_CHIP_PU`。
- C6 `U0TXD/U0RXD` 引到 H4 调试排针。

## 3. 怎么打开

工程依赖：

- `espressif/esp_wifi_remote: ^1.0`
- `espressif/esp_hosted: ^2.4`

在 ESP-IDF PowerShell：

```powershell
cd E:\CodexProject\Wayne_Graduation\DualMCU_AI_Vision_Car\esp32p4_car_controller
idf.py menuconfig
```

选择：

```text
AI Vision Car -> Network backend -> Waveshare ESP32-P4-WIFI6-KIT external ESP32-C6
```

然后编译烧录：

```powershell
idf.py fullclean
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

## 4. 正常现象

串口日志应看到类似信息：

```text
Waveshare ESP32-P4-WIFI6-KIT detected: external ESP32-C6 module
C6 control pins: IO2=P4 GPIO6 CHIP_PU=P4 GPIO54
ESP32-C6 hosted Wi-Fi start, ssid=...
got ip: xxx.xxx.xxx.xxx
```

输入：

```text
diag
```

应看到网络后端为：

```text
waveshare_p4_wifi6_kit_c6
```

## 5. 如果 net 仍然是 0

先看串口日志：

- 如果 `esp_hosted_init failed`，优先检查 `esp_hosted`/`esp_wifi_remote` 组件版本、C6 固件、Waveshare 示例 sdkconfig。
- 如果 Wi-Fi 一直 disconnected，检查 NVS 里的 SSID/密码、路由器是否支持该加密方式、信号是否足够。
- 如果能拿到 IP，`net` 应变为 1，Qwen Realtime 才会启动。

## 6. 下一步需要的资料

如果当前工程仍不能联网，请寻找 Waveshare 或 Espressif 对 ESP32-P4-WIFI6-KIT 的 Wi-Fi 示例工程，重点看：

- 示例工程用的 ESP-IDF 版本。
- P4 和 C6 之间走什么总线。
- 是否包含 `esp_hosted`、`esp_wifi_remote` 或类似组件。
- C6 是否需要单独烧录固件。

拿到这个示例后，再把 `network_manager` 从“诊断后端”升级成真正联网后端。
