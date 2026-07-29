# ESP32-P4 Car Controller

# 2026-06-29 Build Helper

Run in ESP-IDF PowerShell:

```powershell
cd E:\CodexProject\Wayne_Graduation\DualMCU_AI_Vision_Car\esp32p4_car_controller
.\tools\build_esp32p4.ps1
.\tools\build_esp32p4.ps1 -Fullclean
.\tools\build_esp32p4.ps1 -Menuconfig
.\tools\build_esp32p4.ps1 -Port COMx -Flash -Monitor
```

The script runs `preflight_check.ps1`, then `idf.py set-target esp32p4` and `idf.py build`.

# 2026-06-29 IDF v5.5.4 构建失败修复

如果 VS Code ESP-IDF 插件报：

```text
no versions of espressif/ip101 match ...
espressif/ip101 (1.0.0) depends on idf (>=6.0)
```

原因是工程曾经无条件依赖 `espressif/ip101`，但当前 IDF v5.5.4 不满足该组件最新版本要求。现在已修复为：默认 WIFI6-KIT 不下载 `ip101`，只有在 `menuconfig` 明确选择 `Waveshare ESP32-P4-ETH RMII IP101` 以太网后端时才会尝试使用。

修复后建议执行一次清理：

```powershell
idf.py fullclean
idf.py set-target esp32p4
idf.py build
```

# 2026-06-29 Preflight Check 补充

构建前可以先运行：

```powershell
cd E:\CodexProject\Wayne_Graduation\DualMCU_AI_Vision_Car\esp32p4_car_controller
.\tools\preflight_check.ps1
```

它会检查关键文件、NVS 脚本语法、常见密钥泄漏，并提示当前 shell 是否能找到 `idf.py`。

这是双 MCU AI 小车的 ESP32-P4 正式控制入口。

当前已经接入：

- I2C0：GPIO7 SDA / GPIO8 SCL，扫描 ES8311 和 OV5647。
- ES8311：初始化 I2S 和 Codec，不再默认播放测试音。
- OV5647：当前先做 I2C probe，后续由 `image_sampler` 接 MIPI CSI 取帧。
- STM32 UART：UART1，TX GPIO47 / RX GPIO48，115200，使用正式 `AA 55 LEN CMD DATA CHECKSUM` 协议。
- `sensor_cache`：缓存 STM32 上报的距离、温湿度、GPS、电机、云台、行为状态。
- `safety_guard`：ESP32 侧二次限制速度、Tilt 和危险移动。
- `robot_tools`：给 Web/Qwen 调用的安全高层动作入口。
- `secret_manager`：从 NVS 读取 Wi-Fi 和 Qwen API Key，源码不保存真实密钥。
- `network_manager`：读取 Wi-Fi 配置；如果当前 target 支持原生 Wi-Fi，会自动联网，否则等待后续接入板载 Wi-Fi/ETH 驱动。
- `web_debug`：浏览器调试页面，能看状态并控制急停、电机、云台、灯光、行为。
- `qwen_realtime_client`：WebSocket 接入 Qwen Realtime，注册高层工具并分发 Function Calling。

## 构建

在 ESP-IDF PowerShell 中运行：

```powershell
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

## 密钥配置

不要把真实 Wi-Fi 密码和 Qwen API Key 写入源码，也不要写进 `nvs_secrets.example.csv`。

文件规则：

- `config/nvs_secrets.example.csv`：只放占位符，可以提交。
- `config/nvs_secrets.local.csv`：放你的真实 Wi-Fi/API Key，不提交。
- `config/nvs_secrets.local.bin`：由脚本生成，不提交。
- `config/nvs_secrets.csv`：不要再使用；如果误创建，也已被 `.gitignore` 忽略。

如果想手动编辑本地密钥，复制示例文件：

```powershell
copy config\nvs_secrets.example.csv config\nvs_secrets.local.csv
```

只在 `config\nvs_secrets.local.csv` 填真实值。该文件已被 `.gitignore` 忽略。更推荐直接使用 `tools/provision_secrets.ps1`，脚本会自动生成本地 CSV 和 NVS bin。

## 正常启动现象

串口日志应周期显示：

```text
alive stm32=1 net=0 dist=... motor=... gimbal=... behavior=...
```

`stm32=1` 表示 ESP32 已经收到 STM32 状态或心跳回复。未配置 Wi-Fi，或当前 ESP32-P4 target 没有原生 Wi-Fi 驱动时，`net=0` 正常。

配置 Wi-Fi 后，日志会显示 ESP32 的 IP。浏览器打开：

```text
http://ESP32_IP/
```

页面不需要登录，当前用于局域网调试。

## Qwen 测试

配置 Qwen API Key 和网络后，网页里可以在 Qwen 输入框发送文本，例如：

```text
卖个萌
```

模型如果触发 Function Calling，会通过 `robot_tools` 控制 STM32 执行动作。
# 2026-06-29 Camera V4L2 后端补充

工程已预留可选相机真取帧后端。完成板卡级 `esp_video`/OV5647 MIPI CSI 初始化后，在 `menuconfig` 打开：

```text
AI Vision Car -> Enable OV5647 V4L2 JPEG capture backend
```

默认设备为 `/dev/video0`，默认 JPEG 尺寸为 `640x480`。打开后 `capture` 会尝试从 V4L2 取 JPEG 并上传给 Qwen。

你提供的摄像头原理图已确认：

- OV5647 通过 ESP I2C，也就是 P4 GPIO7/GPIO8 配置寄存器。
- MIPI CSI 是 2-lane：`CSI_D0_P/N`、`CSI_D1_P/N`、`CSI_CLK_P/N`。
- 摄像头座还有 `CSI_IO0/CSI_IO1`，其中 `CSI_IO0` 默认 10K 上拉，`CSI_IO1` 当前 NC。

还缺的不是普通 GPIO 引脚，而是 ESP-IDF `esp_video`/V4L2 的板级初始化例程：它要告诉驱动 OV5647 的 lane 数量、时钟、输出格式、是否支持 JPEG、设备节点是否为 `/dev/video0`。没有这个驱动初始化时，`capture` 继续返回 `ESP_ERR_NOT_SUPPORTED` 是正常现象。

# 2026-06-29 Waveshare ESP32-P4-ETH 网络后端补充

如果你的板子是 Waveshare ESP32-P4-ETH 或相同 IP101 RMII 连接，可以在 `menuconfig` 打开：

```text
AI Vision Car -> Network backend -> Waveshare ESP32-P4-ETH RMII IP101
```

预设：MDC GPIO31、MDIO GPIO52、PHY Power GPIO51、RMII Clock GPIO50、PHY address 1。

# 2026-06-29 Waveshare ESP32-P4-WIFI6-KIT 补充

你提供的原理图显示，ESP32-P4-WIFI6-KIT 的联网协处理器是 `ESP32-C6-MINI-1`，不是 ESP32-C4。工程已新增网络后端：

```text
AI Vision Car -> Network backend -> Waveshare ESP32-P4-WIFI6-KIT external ESP32-C6
```

当前代码会拉起 C6 的 `CHIP_PU`，调用 `esp_hosted_init()`，再通过 `esp_wifi_remote` 提供的标准 Wi-Fi STA 接口连接 NVS 里的 Wi-Fi。诊断里会显示 `network_backend=waveshare_p4_wifi6_kit_c6`。

原理图里已确认：

- P4 GPIO6 通过 0R 接 `C6_IO2`。
- P4 GPIO54 通过 0R 接 `C6_CHIP_PU`。
- C6 的 `U0TXD/U0RXD` 引到 H4 调试排针。

如果 `esp_hosted_init` 失败，或一直拿不到 IP，优先检查 ESP-IDF 组件是否下载成功、C6 是否已经烧录匹配的 hosted 固件、板卡 SDIO/hosted 配置是否和 Waveshare 官方示例一致。

# 2026-06-29 Board Diagnostics 补充

串口 monitor 输入：

```text
diag
```

或网页点击 `Diag`，可以查看 `target/network/secrets/qwen/camera/stm32_uart/i2c/image_sampler` 诊断 JSON。

# 2026-06-29 Qwen Runtime Control 补充

串口 monitor 支持：

```text
qwenctl status
qwenctl start
qwenctl restart
qwenctl stop
qwen 你好
```

Web 调试页 Qwen 区域也有 `Start`、`Stop`、`Restart` 按钮。只有 Qwen 状态为 `ready` 时，文本/音频/图片才会发送。

# 2026-06-29 NVS 密钥烧录补充

真实 Wi-Fi 和 Qwen API Key 不写进源码，使用本地 NVS 脚本烧录：

```powershell
cd E:\CodexProject\Wayne_Graduation\DualMCU_AI_Vision_Car\esp32p4_car_controller
.\tools\provision_secrets.ps1 -Port COMx -WifiSsid "你的WiFi名称" -WifiPass "你的WiFi密码" -QwenKey "你的QwenAPIKey"
```

脚本生成的 `config\nvs_secrets.local.csv` 和 `config\nvs_secrets.local.bin` 已被 `.gitignore` 忽略。

# 2026-06-29 Serial Monitor 控制补充

烧录后打开：

```powershell
idf.py -p COMx flash monitor
```

出现 `car>` 后，可以直接输入：

```text
status
stop
led cute
gimbal 90 90 700
gimbal_action scan_slow
behavior cute
move forward 25 600
capture
```

这个入口不依赖 Web 和 Qwen，适合先确认 ESP32-P4 到 STM32 的 UART 控制链路。

# 2026-06-29 Camera Capture 测试补充

Web 调试页 Qwen 区域新增 `Capture` 按钮，也可以让 Qwen 调用 `robot_capture_image`。

当前代码已经接通：`robot_capture_image` -> `image_sampler` -> `camera_ov5647_capture_jpeg()` -> JPG base64 -> `input_image_buffer.append`。

但真实 OV5647 MIPI CSI/JPEG 取帧驱动还没有按具体板卡接入，所以现在点击 `Capture` 的正常日志是：

```text
ov5647: OV5647 JPEG capture is not wired to the ESP32-P4 CSI/JPEG driver yet
image_sampler: image capture failed reason=1 ret=ESP_ERR_NOT_SUPPORTED
```

接入真实相机驱动后，正常应变成：

```text
image_sampler: image upload reason=1 len=xxxxx ret=ESP_OK
```
