# ESP32-P4 小闭环 6：Waveshare ESP32-P4-ETH 网络后端测试

## 1. 目标

为疑似 Waveshare ESP32-P4-ETH 板卡预留 RMII IP101 以太网后端，让 ESP32-P4 有机会真正联网并启动 Qwen Realtime。

默认不启用。只有确认你的板卡网络硬件一致时再打开。

## 2. 当前预设引脚

来自 Waveshare ESP32-P4-ETH 公开资料：

- PHY：IP101
- PHY address：`1`
- MDC：`GPIO31`
- MDIO：`GPIO52`
- PHY Power：`GPIO51`
- RMII Clock：`GPIO50`

工程中对应宏在 `board_config.h`：

```c
BOARD_ETH_MDC_GPIO
BOARD_ETH_MDIO_GPIO
BOARD_ETH_PHY_POWER_GPIO
BOARD_ETH_RMII_CLK_GPIO
BOARD_ETH_PHY_ADDR
```

## 3. 怎么打开

在 ESP-IDF PowerShell：

```powershell
cd E:\CodexProject\Wayne_Graduation\DualMCU_AI_Vision_Car\esp32p4_car_controller
idf.py menuconfig
```

选择：

```text
AI Vision Car -> Network backend -> Waveshare ESP32-P4-ETH RMII IP101
```

然后编译：

```powershell
.\tools\preflight_check.ps1
idf.py build
```

## 4. 怎么烧录

```powershell
idf.py -p COMx flash monitor
```

## 5. 怎么测试

插上网线，进入 monitor 后输入：

```text
diag
net
```

正常联网后应看到：

```text
network backend=waveshare_p4_eth started=1 connected=1
Ethernet got ip: xxx.xxx.xxx.xxx
```

如果已经烧录 Qwen Key，网络连上后 Qwen 会自动启动。

## 6. 正常现象

- `diag` 中 `network.backend` 为 `waveshare_p4_eth`。
- 插网线后 `network.connected=true`。
- 串口日志出现 `Ethernet got ip`。
- 随后 Qwen 出现 `connected` 和 `session updated`。

## 7. 失败先检查什么

1. 板卡是否真的是 Waveshare ESP32-P4-ETH 或同引脚 IP101 RMII 方案。
2. 网线、路由器 DHCP 是否正常。
3. `GPIO31/52/51/50` 是否被其它外设占用。
4. PHY address 是否为 `1`。
5. 如果编译报以太网 API 不匹配，说明 ESP-IDF 版本和当前代码的 Ethernet API 有差异，需要按实际 IDF 版本微调。

## 8. 下一步

网络 `connected=1` 后，再测试 Qwen：

```text
qwen 你好，报一下当前小车状态
```

如果 Qwen 能连接，再继续接 OV5647 `/dev/video0`。
