# STM32 小闭环 8：本地行为执行器测试

## 1. 目标

让 STM32 支持少量本地高层行为，先完成 `manual_control`、`cute`、`sleep`、`wake_up`。行为执行必须非阻塞，主循环仍然持续处理串口、超声波、DHT11、GPS、云台、电机和灯光。

## 2. 新增协议

ESP32-P4 到 STM32：

| 命令 | 值 | DATA |
| --- | --- | --- |
| `CMD_BEHAVIOR_START` | `0x50` | behavior_id |
| `CMD_BEHAVIOR_STOP` | `0x51` | 空 |

STM32 到 ESP32-P4：

| 命令 | 值 | DATA |
| --- | --- | --- |
| `CMD_STATUS_BEHAVIOR` | `0x85` | behavior_id, running, step |

行为 ID：

| ID | 行为 |
| --- | --- |
| `0` | manual_control |
| `1` | cute |
| `2` | sleep |
| `3` | wake_up |

## 3. 测试帧

启动卖萌：

```text
AA 55 02 50 01 53
```

启动休眠：

```text
AA 55 02 50 02 54
```

启动唤醒：

```text
AA 55 02 50 03 55
```

停止行为：

```text
AA 55 01 51 52
```

请求状态：

```text
AA 55 01 40 41
```

## 4. 正常现象

- `cute`：电机立即停止，灯光进入 cute 效果，云台依次歪头、点头、摇头，最后回到手动控制。
- `sleep`：电机停止，云台低头到 Tilt 约 140 度，灯光变暗呼吸。
- `wake_up`：电机停止，灯光亮起闪动，云台左右看后回中。
- 任意行为执行时发送 `CMD_BEHAVIOR_STOP`，电机停止、云台停止、灯光回 idle。
- 手动电机、云台、灯光命令会中断当前行为。

## 5. 失败先检查

1. 行为帧 checksum 是否正确。
2. `behavior_executor.c` 是否已加入 Keil 工程。
3. 云台闭环和 WS2812 闭环是否已经单独测试通过。
4. 超声波是否触发了安全停车，导致电机动作被本地安全层拦截。
5. 串口是否仍按 USART1 115200 8N1、共地、TX/RX 交叉连接。

## 6. 下一步

ESP32-P4 的 `robot_behavior` 工具可以先把 `cute`、`sleep`、`wake_up` 直接映射到 `CMD_BEHAVIOR_START`。`find_user`、`follow_user`、`patrol` 需要摄像头抽帧和 ESP32 行为引擎参与，后续再接入。
