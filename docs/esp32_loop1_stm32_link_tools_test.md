# ESP32-P4 小闭环 1：STM32 正式链路和机器人工具层测试

## 1. 目标

把 ESP32-P4 从“外设测试程序”升级为正式控制入口：

- `stm32_link` 负责 UART1 正式协议。
- `sensor_cache` 缓存 STM32 状态。
- `safety_guard` 做 ESP32 侧二次安全限制。
- `robot_tools` 提供 Qwen/Web 后续要调用的高层工具。
- `secret_manager` 从 NVS 读取 Wi-Fi 和 Qwen API Key，不把真实密钥写进源码。
- `network_manager` 读取 Wi-Fi 配置；如果当前 ESP-IDF target 支持原生 Wi-Fi，会自动联网，否则先保持网络关闭，后续按板载 Wi-Fi/ETH 芯片接驱动。
- `web_debug` 提供浏览器调试页面和 HTTP API。

## 2. 接线

ESP32-P4 和 STM32：

- ESP32-P4 TX `GPIO47` -> STM32 `PA10`
- ESP32-P4 RX `GPIO48` <- STM32 `PA9`
- GND 必须共地
- 波特率 `115200 8N1`

## 3. 编译和烧录

在 ESP-IDF PowerShell 中进入：

```powershell
cd E:\CodexProject\Wayne_Graduation\DualMCU_AI_Vision_Car\esp32p4_car_controller
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

当前普通 PowerShell 环境没有 `idf.py`，所以需要使用 ESP-IDF 自带终端。

## 4. 正常现象

ESP32 启动后会打印：

- `ESP32-P4 AI vision car controller start`
- I2C 扫描结果
- 摄像头 I2C probe 结果
- ES8311 初始化结果
- `stm32_link` UART ready
- `web debug server started`
- 每 5 秒打印一次：

```text
alive stm32=1 net=0 dist=... motor=... gimbal=... behavior=...
```

其中 `stm32=1` 表示 ESP32 已收到 STM32 心跳 ACK 或状态帧。

如果没有配置 Wi-Fi，或当前 ESP32-P4 target 没有原生 Wi-Fi 驱动，`net=0` 是正常的；不影响 STM32 控制。

配置 Wi-Fi 并拿到 IP 后，浏览器打开：

```text
http://ESP32_IP/
```

页面第一屏就是状态和控制按钮，不需要登录。

## 5. 配置 Wi-Fi 和 Qwen 密钥

不要把真实密钥写进源码。复制：

```powershell
copy config\nvs_secrets.example.csv config\nvs_secrets.local.csv
```

然后只在 `nvs_secrets.local.csv` 中填写真实值。该文件已经被 `.gitignore` 忽略。

生成 NVS bin 的常用命令：

```powershell
python %IDF_PATH%\components\nvs_flash\nvs_partition_generator\nvs_partition_gen.py generate config\nvs_secrets.local.csv config\nvs_secrets.local.bin 0x6000
```

写入 NVS 分区可以用 ESP-IDF 的 `parttool.py` 或者在后续分区表固定后用 `esptool.py write_flash`。如果暂时不配置密钥，程序仍会运行 STM32 本地控制闭环。

## 6. 当前 robot_tools 支持

已支持的高层工具入口：

- `robot_tools_stop()`
- `robot_tools_move("forward/backward/left/right/rotate_left/rotate_right", speed, duration_ms)`
- `robot_tools_gimbal_set(pan, tilt, duration_ms)`
- `robot_tools_gimbal_action("center/look_left_soft/look_right_soft/nod_yes/shake_no/scan_slow/curious_tilt")`
- `robot_tools_led_effect("idle/listening/speaking/thinking/error/cute/patrol/solid", r, g, b, duration_ms)`
- `robot_tools_behavior("cute/sleep/wake_up/look_around/patrol/avoid_obstacle")`
- `robot_tools_get_status()`

这些工具只调用 STM32 高层协议，不暴露 PWM。

## 7. Web API

| 接口 | 方法 | 作用 |
| --- | --- | --- |
| `/` | GET | 调试页面 |
| `/api/status` | GET | 返回 STM32 缓存状态 JSON |
| `/api/stop` | POST | 急停 |
| `/api/move?direction=forward&speed=25&duration=600` | POST | 移动 |
| `/api/gimbal?pan=90&tilt=90&duration=700` | POST | 云台角度 |
| `/api/led?effect=cute` | POST | 灯效 |
| `/api/behavior?name=cute` | POST | 行为 |

## 8. 失败先检查

1. ESP32 和 STM32 是否共地。
2. TX/RX 是否交叉。
3. STM32 是否已烧录正式程序，不是 TestOnly。
4. STM32 串口是否为 USART1 `115200 8N1`。
5. ESP32 日志里 `stm32=0` 是否持续超过 5 秒。
6. 若移动命令被拒绝，先看超声波距离是否小于 15 cm，或 STM32 是否离线。
7. 网页打不开时，先看是否配置 Wi-Fi/ETH 并拿到 IP，电脑/手机是否在同一局域网。
8. 如果日志提示当前 target 不支持原生 Wi-Fi，需要按你的 ESP32-P4 WIFI PICO ETH 板载联网方案接入对应驱动。

## 9. 下一步

下一步接 Qwen Realtime 的 WebSocket 和 Function Calling，让模型调用同一套 `robot_tools`。
