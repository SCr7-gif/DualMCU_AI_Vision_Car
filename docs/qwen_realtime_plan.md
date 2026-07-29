# Qwen Realtime 接入计划

## 1. 基本约束

- 使用 WebSocket，不使用 WebRTC。
- API Key 从 `secret_manager` 读取。
- API Key 不写死进源码，不提交 Git。
- 普通聊天只上传音频。
- 图片默认不上传，仅在视觉事件触发时上传。
- 第一版音频使用 half-duplex，不做复杂 AEC。

## 2. qwen_realtime_client 职责

模块位置：`esp32p4_car_controller/components/qwen_realtime_client/`

初始化：

```c
esp_err_t qwen_client_init(void);
esp_err_t qwen_client_start(void);
void qwen_client_stop(void);
```

主任务：

- 建立 WSS 连接。
- 设置 `Authorization: Bearer <API_KEY>`。
- 发送 `session.update`。
- 发送 16 kHz PCM 麦克风音频。
- 接收 24 kHz PCM 回复音频。
- 接收文本 transcript。
- 接收 Function Calling。
- 调用 `robot_tools`。
- 把工具结果返回给 Qwen。
- 断线重连。

## 3. Half-duplex 音频策略

状态：

- `AUDIO_STATE_LISTENING`：上传麦克风音频。
- `AUDIO_STATE_SPEAKING`：播放 Qwen 音频，暂停麦克风上传。
- `AUDIO_STATE_IDLE`：待机。

流程：

1. 用户说话时，麦克风 PCM 分片上传。
2. 收到 Qwen 音频回复后，切换到 speaking。
3. speaking 期间停止上传麦克风。
4. 播放结束后恢复 listening。

## 4. Function Calling 工具

工具只能提供高层能力：

- `robot_get_status`
- `robot_stop`
- `robot_move`
- `robot_gimbal_set`
- `robot_gimbal_action`
- `robot_led_effect`
- `robot_capture_image`
- `robot_behavior`
- `robot_say`

禁止提供：

- 直接设置 PWM。
- 直接设置 GPIO。
- 直接写定时器寄存器。
- 绕过 `safety_guard` 的运动命令。

## 5. 图片上传策略

`image_sampler` 默认关闭上传。

触发条件：

- 用户问“你看到了什么”。
- 用户要求“看看周围”。
- 用户要求“找找我”。
- follow_user、find_user、patrol 等行为启动。

上传频率：

- 默认 1 FPS。
- 可短时间 burst 2 到 3 张，间隔 300 到 500 ms。
- 休眠和普通聊天不上传。

## 6. 第一闭环

第一闭环只做：

- secret_manager 能读取占位配置。
- Qwen 客户端检测到无 API Key 时不上线并输出明确错误。
- Web 页面显示 Qwen 未配置。

第二闭环再做：

- WebSocket 连接。
- 发送 session.update。
- 打印连接状态。

第三闭环再做：

- half-duplex 音频。

