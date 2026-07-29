# 正式开发任务清单

## 0. 当前证据

已验证状态：

- ESP32-P4 I2C 能扫到设备。
- 音频能出声。
- UART 能和 STM32 通信。
- 摄像头能初始化。
- STM32 CubeMX 配置满足 USART1、TIM1/2/3/4、USART2、DHT11、WS2812、SWD 基础要求。

本轮安全检查：

- 未发现真实 API Key 被写入工程文件。
- 不把 pasted 文本里的真实 API Key 和 Wi-Fi 密码写入源码。

## 1. STM32 修改计划

### 1.1 协议闭环

文件：

- `User/Protocol/car_protocol.c/.h`
- `User/Protocol/command_handler.c/.h`
- `User/App/app_main.c/.h`

目标：

- USART1 接收 `AA 55 LEN CMD DATA CHECKSUM`。
- 支持 `CMD_HEARTBEAT`、`CMD_REQUEST_STATUS`、`CMD_STOP`。
- 能返回 `CMD_HEARTBEAT_ACK`、`CMD_STATUS_BASIC`、`CMD_ERROR`。

验收：

- 上位机或 ESP32 发送心跳，STM32 返回 ACK。
- 校验失败返回错误或丢帧，不死机。
- 电机仍保持停止。

### 1.2 电机 ramp

文件：

- `User/BSP/bsp_motor.c/.h`
- `User/App/car_control.c/.h`
- `User/App/safety_manager.c/.h`

目标：

- `Motor_RampToSpeed(target, duration_ms)` 设置目标速度。
- `Motor_Update(now_ms)` 在主循环中逐步逼近目标速度。
- STOP 立即把目标速度和 PWM 清零。

验收：

- 低速前进从 0 平滑到目标速度。
- STOP 立即停车。
- 通信超时触发停车。

### 1.3 DHT11 修复

文件：

- `User/BSP/bsp_dht11.c/.h`

目标：

- PB5 输出拉低 18 ms。
- 拉高 20 到 40 us。
- 切换输入上拉。
- 等待响应和 40 位数据都有 timeout。
- 最小读取间隔 2 秒。

验收：

- 正常读取温湿度。
- 拔掉 DHT11 不会卡死主循环。
- 读取失败上报错误状态。

### 1.4 Gimbal Motion Engine

文件：

- `User/Utils/easing.c/.h`
- `User/App/gimbal_control.c/.h`
- `User/BSP/bsp_servo.c/.h`

目标：

- 支持关键帧、hold、ease_in_out、smooth_step。
- 支持动作中断。
- 支持当前角度读取。
- Tilt 限制 30 到 150。

验收：

- 回中平滑。
- look_left_soft、nod_yes、shake_no 动作自然。
- 下发 Tilt 0 或 180 时被限制到 30 或 150。

当前进度：

- 已接入 STM32 云台小闭环：TIM4_CH1/PB6 Pan、TIM4_CH2/PB7 Tilt。
- 已实现整数缓动、关键帧队列、动作中断、状态上报和 Tilt 30 到 150 度硬限制。
- behavior_executor 已开始调用云台动作，当前先服务 cute、sleep、wake_up 三个本地行为。

### 1.5 传感器和灯光

文件：

- `User/BSP/bsp_ultrasonic.c/.h`
- `User/BSP/bsp_gps.c/.h`
- `User/BSP/bsp_ws2812.c/.h`

目标：

- 超声波定期测距，小于 15 cm 自动停车。
- GPS 缓存 NMEA 原始行。
- WS2812 支持 RGB 和状态效果。

验收：

- 距离过近时无论当前命令是什么都停车。
- GPS 有数据时上报原始 NMEA。
- idle/listening/speaking/error 灯效可切换。

当前进度：

- HC-SR04 小闭环已接入 STM32：PA1 触发，PA0/TIM2 捕获 Echo，距离小于 15 cm 时本地停车。
- WS2812 小闭环已接入 STM32：PA8/TIM1/DMA 输出，支持 RGB、idle/listening/speaking/thinking/error/cute/patrol 灯效。
- GPS 小闭环已接入 STM32：USART2 轮询接收 NMEA，缓存最近一行，通过 `CMD_STATUS_GPS = 0x82` 上报。

### 1.6 本地行为执行器

文件：

- `User/App/behavior_executor.c/.h`
- `User/Protocol/command_handler.c/.h`
- `User/Protocol/car_protocol.h`

目标：

- 支持 `manual_control`、`cute`、`sleep`、`wake_up`。
- 行为用 `Behavior_Start`、`Behavior_Update`、`Behavior_Stop` 非阻塞推进。
- 行为只调用电机、云台、灯光的高层接口，不直接碰 PWM。
- 急停、通信超时、超声波避障仍由 safety_manager 保持最高优先级。

验收：

- `cute` 能组合云台动作和 WS2812 cute 灯效。
- `sleep` 能停车、低头、灯光变暗。
- `wake_up` 能抬头左右看、灯光亮起。
- 手动电机、云台、灯光命令能中断当前行为。

当前进度：

- 已新增 `CMD_BEHAVIOR_START = 0x50`、`CMD_BEHAVIOR_STOP = 0x51`。
- 已新增 `CMD_STATUS_BEHAVIOR = 0x85`，payload 为 `behavior_id, running, step`。

## 2. ESP32-P4 修改计划

### 2.1 secret_manager

目标：

- 从 NVS、menuconfig 或本地 config 文件读取 Wi-Fi 和 Qwen API Key。
- 源码只提交 example，不提交真实密钥。

验收：

- 未配置 API Key 时 Qwen 模块不启动，并在 Web 页面和日志提示。
- 配置后能读取但不会打印完整密钥。

当前进度：

- ESP32 已新增 `secret_manager`，从 NVS namespace `robot_secret` 读取 `wifi_ssid`、`wifi_pass`、`qwen_key`、`qwen_model`、`qwen_endpoint`。
- 已新增 `config/nvs_secrets.example.csv`，真实 `nvs_secrets.local.csv` 已加入 `.gitignore`。

### 2.2 stm32_link

目标：

- 替换当前 `stm32_uart` 一阶段测试任务。
- 实现正式协议收发、心跳、状态解析、超时检测。

验收：

- Web 页面能显示 STM32 在线。
- 手动 STOP 能到达 STM32。
- STM32 断线时 ESP32 显示离线并拒绝移动。

当前进度：

- ESP32 已新增正式 `stm32_link`，使用 UART1 GPIO47/GPIO48 和 `AA 55 LEN CMD DATA CHECKSUM`。
- 已支持心跳、请求状态、STOP、移动、云台、灯效、行为命令。
- 已解析 STM32 `0x80` 到 `0x85` 状态帧并写入 `sensor_cache`。
- 已新增 `safety_guard`，前进前检查 STM32 在线和超声波距离，速度上限 60%，Tilt 限制 30 到 150。
- 已新增 `robot_tools`，提供 `robot_stop`、`robot_move`、`robot_gimbal_action`、`robot_led_effect`、`robot_behavior`、`robot_get_status` 的 C 层入口。
- 已新增 `network_manager`，当前对原生 Wi-Fi 做条件编译；ESP32-P4 外接 Wi-Fi/ETH 方案后续按实际板卡驱动接入。

### 2.3 Web 调试页面

目标：

- 显示 Qwen、STM32、麦克风、喇叭、摄像头、距离、温湿度、GPS、云台、电机、行为、日志。
- 提供急停按钮。

验收：

- 手机浏览器能访问。
- 急停按钮 1 次点击触发 STM32 STOP。

当前进度：

- 已新增 `web_debug` HTTP server。
- 已提供 `/api/status`、`/api/stop`、`/api/move`、`/api/gimbal`、`/api/led`、`/api/behavior`。
- 已提供 `/api/qwen`，可从网页发送文本给 Qwen。
- 已提供一个局域网调试页面，打开 ESP32 IP 即可查看状态和触发基础控制。

### 2.4 Qwen Realtime

目标：

- WebSocket 连接。
- Bearer API Key 鉴权。
- session.update。
- half-duplex 音频。
- Function Calling 工具执行和结果回传。

验收：

- 普通语音聊天只上传音频。
- 播放回复时暂停麦克风上传。
- 工具调用不会直接暴露 PWM。

当前进度：

- 已新增 `qwen_realtime_client`，通过 WebSocket 连接 endpoint。
- 已从 `secret_manager` 读取 `qwen_key`、`qwen_model`、`qwen_endpoint`，不在源码写真实密钥。
- 已发送 `session.update`，注册 `robot_get_status`、`robot_stop`、`robot_move`、`robot_gimbal_set`、`robot_gimbal_action`、`robot_led_effect`、`robot_behavior`。
- 已处理 `response.function_call_arguments.done`，把工具调用转给 `robot_tools`，并回传工具结果。
- 已处理 `response.audio.delta`，base64 解码后通过 ES8311 播放。
- 已添加麦克风 PCM 上传任务，播放回复时暂停上传，作为第一版半双工。

### 2.5 image_sampler

目标：

- 默认不上传图片。
- 视觉请求或行为触发时 1 FPS 上传 JPG。
- follow_user 实时控制优先本地视觉，Qwen 只低频判断。

验收：

- 普通聊天没有图片上传日志。
- “你看到了什么”触发单张或 1 FPS 上传。

当前进度：

- 已新增 `image_sampler` 低频/事件触发框架。
- 默认不上传图片。
- `look_around` 触发单张入口，`find_user`、`follow_user`、`patrol` 触发 1 FPS 入口。
- 当前已接入 `camera_ov5647_capture_jpeg()` 上层边界和 JPG base64 上传流程；真实 OV5647 MIPI CSI/JPEG 取帧驱动仍需按实际 ESP32-P4 板卡继续接入。

## 3. 不做事项

- 不把真实 API Key 写入源码。
- 不把 TestOnly 当正式业务代码。
- 不一次性写完整系统。
- 不在第一版做复杂 AEC。
- 不让 Qwen 直接控制 PWM。
# 2026-06-29 ESP32-P4 补充进度

- 已新增 `tools/provision_secrets.ps1`，可在 ESP-IDF PowerShell 中生成并烧录本地 NVS 密钥分区；真实 Wi-Fi/Qwen Key 不写进源码。
- 已新增 `tools/preflight_check.ps1`，构建前检查关键文件、NVS 脚本语法、密钥泄漏和当前 shell 的 `idf.py`。
- 已新增 `tools/build_esp32p4.ps1`，用于一键 preflight、set-target、build、可选 menuconfig/flash/monitor。
- 已新增 `board_diagnostics`，串口 `diag` 和 Web `/api/diagnostics` 可输出 ESP32-P4 板级配置与运行状态摘要。
- 已新增可选 `Waveshare ESP32-P4-ETH RMII IP101` 网络后端；确认板卡匹配后可在 menuconfig 打开，尝试通过以太网让 `net=1`。
- 已新增 `Waveshare ESP32-P4-WIFI6-KIT external ESP32-C6` 网络后端选项；当前会拉起 C6 `CHIP_PU`，调用 `esp_hosted_init()`，并通过 `esp_wifi_remote` 的标准 STA 流程连接 NVS Wi-Fi。
- 已修复 IDF v5.5.4 下 `espressif/ip101` 依赖导致的构建失败：默认 WIFI6-KIT 不再下载 IP101 组件。
- 已调整 Qwen 启动逻辑：只有 `network_manager_is_connected()` 后才启动 Realtime，避免 ESP32-P4 未接外部网络驱动时反复 WebSocket 报错。
- 已新增 Qwen 运行时控制：串口 `qwenctl status/start/stop/restart`，Web `/api/qwen_control` 和 Start/Stop/Restart 按钮。
- 已新增 `console_control` 串口 monitor 控制入口，可直接输入 `status/stop/move/gimbal/led/behavior/capture/qwen` 测试，不依赖 Web 和 Qwen。
- 已新增 `robot_capture_image` 高层工具，Qwen 和 Web 调试页都可以触发单次拍照请求。
- 已把 `image_sampler` 从占位日志改成真实上传链路：相机取 JPEG、base64 编码、通过 `input_image_buffer.append` 发给 Qwen。
- 已新增 `camera_ov5647_capture_jpeg()` 接口边界；默认没有伪造图片，真实 ESP32-P4 MIPI CSI/JPEG 驱动未接入时返回 `ESP_ERR_NOT_SUPPORTED`。
- 已新增可选 V4L2 JPEG capture 后端，打开 `CONFIG_ROBOT_CAMERA_V4L2_CAPTURE` 后可从 `/dev/video0` 抓取 JPEG；前提是板卡级 `esp_video` 已经完成 OV5647 MIPI CSI 初始化。
- 已根据用户原理图记录 OV5647 摄像头座：2-lane MIPI CSI，ESP I2C 为 GPIO7/GPIO8，`CSI_IO0` 默认上拉，`CSI_IO1` 当前 NC。
- 已扩展 STM32 云台自然动作库：look_up_soft、look_down_soft、scan_fast、sleepy、wake_up、shy_look_down、happy_wiggle、thinking_motion。
- 已新增 `docs/esp32_loop3_image_sampler_camera_test.md`、`docs/esp32_loop4_console_control_test.md`、`docs/esp32_loop5_nvs_secret_provision_test.md`、`docs/esp32_loop6_waveshare_eth_backend_test.md` 和 `docs/esp32_loop7_waveshare_wifi6_kit_test.md`，用于测试相机上传链路、串口控制链路、NVS 密钥烧录、网络后端和说明下一步。
