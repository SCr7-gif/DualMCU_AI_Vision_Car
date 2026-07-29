# ESP32-P4 与 STM32 UART 协议

## 1. 帧格式

```text
AA 55 LEN CMD DATA... CHECKSUM
```

- `AA 55`：固定帧头。
- `LEN`：`CMD + DATA` 的总字节数。
- `CMD`：命令字。
- `DATA`：参数。
- `CHECKSUM`：`LEN + CMD + DATA 所有字节` 的低 8 位。

## 2. 通用规则

- USART1，115200，8N1。
- STM32 收到未知命令返回 `CMD_ERROR`。
- 校验错误直接丢帧，可累计错误计数。
- ESP32 每 500 ms 到 1000 ms 发送一次心跳。
- STM32 超过 3000 ms 没收到有效命令或心跳，自动停车。
- 急停命令优先级最高，不进入队列。

## 3. ESP32-P4 到 STM32

| 命令 | 值 | DATA | 说明 |
| --- | --- | --- | --- |
| `CMD_STOP` | `0x10` | 空 | 急停/停车 |
| `CMD_MOVE_FORWARD` | `0x11` | speed, duration_l, duration_h | 前进 |
| `CMD_MOVE_BACKWARD` | `0x12` | speed, duration_l, duration_h | 后退 |
| `CMD_TURN_LEFT` | `0x13` | speed, duration_l, duration_h | 左转 |
| `CMD_TURN_RIGHT` | `0x14` | speed, duration_l, duration_h | 右转 |
| `CMD_ROTATE_LEFT` | `0x15` | speed, duration_l, duration_h | 原地左转 |
| `CMD_ROTATE_RIGHT` | `0x16` | speed, duration_l, duration_h | 原地右转 |
| `CMD_SET_SPEED` | `0x17` | speed | 设置速度上限内目标速度 |
| `CMD_GIMBAL_CENTER` | `0x20` | duration_l, duration_h | 云台平滑回中 |
| `CMD_SET_PAN` | `0x21` | pan, duration_l, duration_h, easing | 设置 Pan |
| `CMD_SET_TILT` | `0x22` | tilt, duration_l, duration_h, easing | 设置 Tilt |
| `CMD_SET_PAN_TILT` | `0x23` | pan, tilt, duration_l, duration_h, easing | 设置 Pan/Tilt |
| `CMD_GIMBAL_ACTION` | `0x24` | action_id | 执行动作序列 |
| `CMD_LED_RGB` | `0x30` | r, g, b | 设置 RGB |
| `CMD_LED_EFFECT` | `0x31` | effect_id, r, g, b, duration_l, duration_h | 设置灯效 |
| `CMD_REQUEST_STATUS` | `0x40` | 空 | 请求状态 |
| `CMD_BEHAVIOR_START` | `0x50` | behavior_id | 启动 STM32 本地高层行为 |
| `CMD_BEHAVIOR_STOP` | `0x51` | 空 | 停止当前行为并回到手动控制 |
| `CMD_HEARTBEAT` | `0x7F` | seq | 心跳 |

速度范围为 0 到 100，但 STM32 必须再应用本地速度上限。Tilt 范围硬限制为 30 到 150。

当前 STM32 小闭环 2 已实现电机运动命令。`duration` 使用小端毫秒值，`0` 表示持续运动直到 STOP 或通信超时；非 0 持续时间到期后会平滑降速停车。

当前 STM32 小闭环 3 已实现 DHT11 温湿度读取，并通过 `CMD_STATUS_SENSOR = 0x81` 上报。

当前 STM32 小闭环 4 已实现 HC-SR04 超声波测距。`dist_l/dist_h` 为距离 `cm x10` 小端值，`0xFFFF` 表示本次距离无效或超时。距离有效且小于 15 cm 时，STM32 本地安全模块会立即停车，并设置 `CMD_STATUS_BASIC.flags` 的障碍过近标志。

当前 STM32 小闭环 5 已实现云台 Motion Engine。云台命令只接收角度、时长、缓动和动作 ID，不暴露 TIM4 PWM。Tilt 会在 STM32 侧硬限制到 30 到 150 度，新命令会中断当前动作并启动新的平滑关键帧。

云台缓动类型：

| 值 | 含义 |
| --- | --- |
| `0` | linear |
| `1` | ease_in_out |
| `2` | smooth_step |

云台动作 ID：

| 值 | 含义 |
| --- | --- |
| `0` | center |
| `1` | look_left_soft |
| `2` | look_right_soft |
| `3` | nod_yes |
| `4` | shake_no |
| `5` | scan_slow |
| `6` | curious_tilt |

当前 STM32 小闭环 6 已实现 WS2812 状态灯。`CMD_LED_RGB` 用于直接设置 RGB；`CMD_LED_EFFECT` 用于设置状态灯效和可选持续时间。灯效由 STM32 本地非阻塞刷新，不影响电机、云台、传感器和急停。

LED 灯效 ID：

| 值 | 含义 |
| --- | --- |
| `0` | idle |
| `1` | listening |
| `2` | speaking |
| `3` | thinking |
| `4` | error |
| `5` | cute |
| `6` | patrol |
| `7` | solid |

`CMD_LED_EFFECT` payload 为 `effect_id, r, g, b, duration_l, duration_h`。`duration = 0` 表示持续执行；非 0 表示毫秒计时，到期后自动回到 idle。

当前 STM32 小闭环 7 已实现 GPS NMEA 缓存。USART2 使用 9600 8N1，`CMD_STATUS_GPS = 0x82` payload 为最近一行 NMEA ASCII，不带 `\r\n`。如果尚未收到完整 NMEA 行，payload 为空。受当前协议最大数据长度限制，超过 64 字节的 NMEA 行会上报前 64 字节。

当前 STM32 小闭环 8 已实现本地行为执行器。`CMD_BEHAVIOR_START = 0x50` payload 为 `behavior_id`，当前支持：

| 值 | 含义 |
| --- | --- |
| `0` | manual_control |
| `1` | cute |
| `2` | sleep |
| `3` | wake_up |

`CMD_BEHAVIOR_STOP = 0x51` 无 payload，会停止电机、停止云台动作、灯光回 idle，并把当前行为切回 manual_control。手动电机、云台、灯光命令会中断正在执行的行为。

## 4. STM32 到 ESP32-P4

| 命令 | 值 | DATA | 说明 |
| --- | --- | --- | --- |
| `CMD_STATUS_BASIC` | `0x80` | flags, uptime_l, uptime_h | 在线、急停、错误标志 |
| `CMD_STATUS_SENSOR` | `0x81` | dist_l, dist_h, temp, hum, dht_ok | 距离 cm x10、温湿度 |
| `CMD_STATUS_GPS` | `0x82` | ASCII NMEA 或解析数据 | GPS |
| `CMD_STATUS_GIMBAL` | `0x83` | pan, tilt, moving, action_id | 云台状态 |
| `CMD_STATUS_MOTOR` | `0x84` | state, target_speed, current_speed | 电机状态 |
| `CMD_STATUS_BEHAVIOR` | `0x85` | behavior_id, running, step | 当前本地行为状态 |
| `CMD_ERROR` | `0xE0` | error_code, detail | 错误 |
| `CMD_HEARTBEAT_ACK` | `0xFF` | seq | 心跳回复 |

### `CMD_STATUS_BASIC.flags`

| bit | 值 | 含义 |
| --- | --- | --- |
| 0 | `0x01` | 电机当前处于停止状态 |
| 1 | `0x02` | 通信超时，STM32 已停车 |
| 2 | `0x04` | 超声波距离小于 15 cm，STM32 已停车 |
| 3 | `0x08` | 超声波最近一次测距超时或无效 |

## 5. 错误码

| 错误码 | 含义 |
| --- | --- |
| `0x01` | 校验失败 |
| `0x02` | 未知命令 |
| `0x03` | 参数长度错误 |
| `0x04` | 参数越界 |
| `0x05` | 安全拒绝：距离过近 |
| `0x06` | 安全拒绝：急停状态 |
| `0x07` | 通信超时停车 |
| `0x08` | DHT11 读取失败 |

## 6. 第一闭环测试

最小闭环只实现：

- `CMD_HEARTBEAT`
- `CMD_HEARTBEAT_ACK`
- `CMD_REQUEST_STATUS`
- `CMD_STATUS_BASIC`
- `CMD_STOP`
- `CMD_STATUS_MOTOR`

只有这个闭环稳定后，再接入电机、云台、传感器。
