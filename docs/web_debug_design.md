# Web 调试页面设计

## 1. 目标

Web 页面是正式项目的调试入口，不能删除。它用于验证 ESP32-P4、STM32、Qwen、音频、摄像头和行为状态。

## 2. 页面功能

必须显示：

- Qwen 连接状态。
- STM32 在线状态。
- 麦克风状态。
- 喇叭状态。
- 摄像头状态。
- HC-SR04 距离。
- DHT11 温度和湿度。
- GPS 原始数据或解析结果。
- Pan/Tilt 当前角度。
- 电机状态和当前速度。
- 当前行为。
- image_sampler 是否正在上传图片。
- 日志。

必须提供按钮：

- 急停。
- 请求状态。
- 云台回中。
- Pan/Tilt 手动控制。
- 电机低速前进、后退、左转、右转。
- 触发找人。
- 触发跟随。
- 触发巡逻。
- 触发卖萌。
- 进入睡眠。
- 唤醒。

## 3. 安全规则

- 急停按钮最高优先级，直接调用 `robot_stop`，再通过 `stm32_link` 发 `CMD_STOP`。
- Web 手动控制也必须经过 `safety_guard`。
- Web 页面不显示完整 API Key。
- Web 页面不提供底层 PWM 控制。

## 4. 第一闭环

只实现：

- HTTP 服务启动。
- 首页能打开。
- 显示 ESP32 uptime。
- 显示 STM32 online/offline。
- 显示日志最近 20 条。
- 急停按钮可用。

验收：

- 手机浏览器能访问 ESP32 IP。
- 点击急停后串口看到 STM32 STOP 帧。

## 5. 数据来源

| 页面字段 | 来源模块 |
| --- | --- |
| Qwen 状态 | `qwen_realtime_client` |
| STM32 状态 | `stm32_link` |
| 距离/温湿度/GPS | `sensor_cache` |
| 云台/电机 | `sensor_cache` |
| 麦克风/喇叭 | `audio_es8311` |
| 摄像头 | `camera_ov5647` |
| 图片上传状态 | `image_sampler` |
| 当前行为 | `behavior_engine` |
| 日志 | `web_debug` 环形日志缓存 |

## 6. 页面结构

第一版使用普通 HTML + 简单 CSS + `/api/status` JSON 轮询，不引入前端框架。

接口建议：

- `GET /`：调试页面。
- `GET /api/status`：状态 JSON。
- `POST /api/stop`：急停。
- `POST /api/gimbal`：云台动作。
- `POST /api/move`：低速手动移动。
- `POST /api/behavior`：行为触发。
- `GET /api/logs`：日志。

