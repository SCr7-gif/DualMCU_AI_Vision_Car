# ESP32-P4 小闭环 2：Qwen Realtime 与 Function Calling 测试

## 1. 目标

让 ESP32-P4 通过 WebSocket 接入 Qwen3.5-Omni-Realtime：

- 使用 NVS 中的 `qwen_key` 鉴权。
- 启动后发送 `session.update`。
- 注册安全高层工具，不暴露 PWM、GPIO 或底层 STM32 协议。
- 接收 `response.function_call_arguments.done` 后调用 `robot_tools`。
- 把工具执行结果通过 `conversation.item.create` 返回给模型。
- 支持 `response.audio.delta` 解码播放。
- 麦克风第一版按半双工策略上传 PCM，播放 Qwen 音频时暂停上传。

## 2. 已注册工具

- `robot_get_status`
- `robot_stop`
- `robot_capture_image`
- `robot_move`
- `robot_gimbal_set`
- `robot_gimbal_action`
- `robot_led_effect`
- `robot_behavior`

`robot_behavior` 当前支持：

- `find_user`
- `follow_user`
- `patrol`
- `cute`
- `sleep`
- `wake_up`
- `look_around`
- `avoid_obstacle`

其中视觉相关行为当前只会触发 `image_sampler` 低频上传入口；真实 OV5647 MIPI CSI/JPEG 取帧还需要下一步接入。

## 3. 编译和烧录

在 ESP-IDF PowerShell：

```powershell
cd E:\CodexProject\Wayne_Graduation\DualMCU_AI_Vision_Car\esp32p4_car_controller
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

## 4. 配置密钥

真实 Wi-Fi 和 Qwen API Key 不写源码。复制：

```powershell
copy config\nvs_secrets.example.csv config\nvs_secrets.local.csv
```

只在 `nvs_secrets.local.csv` 填真实值，然后生成并烧录到 NVS。

也可以直接使用脚本：

```powershell
.\tools\provision_secrets.ps1 -Port COMx -WifiSsid "你的WiFi名称" -WifiPass "你的WiFi密码" -QwenKey "你的QwenAPIKey"
```

## 5. 测试方法

ESP32 联网并启动 Qwen 后，日志应看到：

```text
qwen_realtime: connected
qwen_realtime: session updated
```

也可以在 monitor 中手动控制：

```text
net
qwenctl status
qwenctl start
qwen 你好，报一下当前小车状态
qwenctl restart
qwenctl stop
```

Web 调试页 Qwen 区域也提供 `Start`、`Stop`、`Restart` 按钮。

网页打开：

```text
http://ESP32_IP/
```

在 Qwen 输入框发送：

```text
卖个萌
```

如果模型触发 `robot_behavior`，日志会出现：

```text
function call: robot_behavior args=...
tool robot_behavior cute
```

STM32 正常会执行 cute 行为。

## 6. 正常现象

- Qwen 连接成功后 `qwen_state` 从 connecting 变为 connected。
- 如果 ESP32-P4 外接网络驱动还没接入，Qwen 会等待网络连上后再启动，`net=0` 时不会反复启动 WebSocket。
- 只有 `qwen_state` 进入 `ready` 后，`qwen <text>` 和网页 Send 才会真正发送；否则返回失败是正常的。
- Qwen 文本请求能触发 Function Calling。
- 工具调用不会直接控制 PWM，只会走 `robot_tools`。
- Qwen 返回音频 delta 时，ES8311 喇叭播放 PCM。
- 播放期间麦克风上传暂停，避免第一版半双工串音。

## 7. 失败先检查

1. `qwen_key` 是否已经写入 NVS。
2. ESP32 是否真的联网并能访问公网。
3. endpoint/model 是否和你的 DashScope/Qwen Realtime 权限匹配。
4. 日志是否有 WebSocket 连接错误。
5. STM32 是否在线，否则工具调用可能被 `safety_guard` 拒绝。
6. 如果没有工具调用，先用明确语句测试，例如“调用 robot_behavior 执行 cute”。

## 8. 下一步

下一步要接 OV5647 的真实 MIPI CSI/JPEG 取帧，让 `image_sampler` 能把真实图片发给 Qwen。
