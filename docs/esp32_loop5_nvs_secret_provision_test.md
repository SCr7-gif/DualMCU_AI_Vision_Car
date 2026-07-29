# ESP32-P4 小闭环 5：Wi-Fi 和 Qwen 密钥烧录测试

## 1. 目标

把 Wi-Fi 名称、Wi-Fi 密码、Qwen API Key 写入 ESP32-P4 的 NVS，不写进源码。

程序启动后 `secret_manager` 会从 NVS namespace `robot_secret` 读取：

- `wifi_ssid`
- `wifi_pass`
- `qwen_key`
- `qwen_model`
- `qwen_endpoint`

## 2. 怎么编译

```powershell
cd E:\CodexProject\Wayne_Graduation\DualMCU_AI_Vision_Car\esp32p4_car_controller
idf.py set-target esp32p4
idf.py build
```

## 3. 怎么烧录程序

```powershell
idf.py -p COMx flash monitor
```

## 4. 怎么烧录密钥

在 ESP-IDF PowerShell 中运行：

```powershell
cd E:\CodexProject\Wayne_Graduation\DualMCU_AI_Vision_Car\esp32p4_car_controller
.\tools\provision_secrets.ps1 -Port COMx -WifiSsid "你的WiFi名称" -WifiPass "你的WiFi密码" -QwenKey "你的QwenAPIKey"
```

脚本会生成：

- `config\nvs_secrets.local.csv`
- `config\nvs_secrets.local.bin`

这两个文件已被 `.gitignore` 忽略。

## 5. 怎么测试

烧录密钥后重启 ESP32-P4，monitor 里应看到类似：

```text
secret_manager: wifi_configured=1 qwen_key_configured=1 model=qwen3.5-omni-flash-realtime-2026-03-15
```

如果网络驱动可用，并且板子真的能联网，后续会看到 Qwen 连接：

```text
qwen_realtime: connected
qwen_realtime: session updated
```

如果密钥已经烧录，但 ESP32-P4 外接网络驱动还没通，会看到：

```text
network_manager: Wi-Fi secrets are configured, but this ESP-IDF target has no native Wi-Fi driver
app_main: qwen key exists, but network is not connected; waiting for external Wi-Fi/ETH driver
```

这说明密钥读取是好的，但网络硬件/驱动还没完成。

## 6. 正常现象

- 没烧录密钥时，日志提示 NVS namespace 不存在或未配置，这是正常的。
- 烧录后 `wifi_configured=1 qwen_key_configured=1`。
- ESP32-P4 如果没有原生 Wi-Fi 或外接网络驱动，`net=0` 仍可能正常；这说明密钥有了，但网络硬件/驱动还没通。Qwen 会等网络连上后再启动。

## 7. 失败先检查什么

1. 是否在 ESP-IDF PowerShell 里运行，`IDF_PATH` 是否存在。
2. `COMx` 是否是 ESP32-P4 的下载串口。
3. NVS offset 是否为默认 `0x9000`。如果你改过分区表，要改脚本参数 `-NvsOffset`。
4. `secret_manager` 日志是否仍显示未配置。
5. 如果密钥已配置但 Qwen 不连接，先查 ESP32-P4 网络是否真的可用。

## 8. 下一步

密钥烧录成功后，继续确认 ESP32-P4 的实际联网硬件：原生 Wi-Fi 不适用于 ESP32-P4，需要板载外接 Wi-Fi/以太网驱动或其它联网方案。
