# STM32 小闭环 4：HC-SR04 超声波安全停车测试

## 1. 目标

让 STM32 周期性读取 HC-SR04 距离，并把距离写入 `CMD_STATUS_SENSOR = 0x81`。当有效距离小于 15 cm 时，STM32 不依赖 ESP32 或 Qwen，直接本地停车。

## 2. 硬件连接

| HC-SR04 | STM32 |
| --- | --- |
| TRIG | PA1 |
| ECHO | PA0 / TIM2_CH1 |
| VCC | 5V 或模块要求电源 |
| GND | GND |

注意：STM32F103 IO 是 3.3V 电平。如果你的 HC-SR04 ECHO 输出是 5V，需要用分压或电平转换后再接 PA0。

## 3. 实现策略

- TIM2 已配置为 1 MHz 输入捕获。
- `BspUltrasonic_Update()` 每 100 ms 触发一次测距。
- PA1 输出 10 us 触发脉冲。
- TIM2 捕获 PA0 Echo 脉宽。
- 距离换算：`distance_cm_x10 = echo_us * 10 / 58`。
- 35 ms 没收到 Echo 时判定超时，距离上报 `0xFFFF`。
- `SafetyManager_Update()` 发现有效距离小于 150 mm 时立即停车。

## 4. 状态帧

请求状态：

```text
AA 55 01 40 41
```

传感器状态：

```text
AA 55 06 81 dist_l dist_h temp hum dht_ok checksum
```

距离字段：

- `dist = dist_l + dist_h * 256`
- 有效时单位为 `cm x10`
- `150` 表示 15.0 cm
- `0xFFFF` 表示超时或无效

基础状态 `flags`：

| 标志 | 含义 |
| --- | --- |
| `0x04` | 障碍物小于 15 cm，已停车 |
| `0x08` | 超声波最近一次测距超时或无效 |

## 5. 测试步骤

1. 接好 HC-SR04，确认 ECHO 到 STM32 PA0 是 3.3V 安全电平。
2. 烧录 STM32。
3. 上电后等待 1 秒。
4. 发送 `AA 55 01 40 41`，查看 `0x81` 中的距离。
5. 把障碍物放到 15 cm 以内。
6. 发送前进命令或保持电机运动，观察 STM32 是否自动停车。
7. 移开障碍物，再请求状态，确认 `flags` 的 `0x04` 清除。

## 6. 正常现象

- 空旷或正常距离时，`dist_l/dist_h` 为合理距离值。
- Echo 超时时，距离为 `0xFFFF`，基础状态 `flags` 带 `0x08`。
- 障碍物小于 15 cm 时，基础状态 `flags` 带 `0x04`，电机状态变为 STOP。
- 障碍物未移开时，运动命令会被 `CAR_ERROR_SAFETY = 0x05` 拒绝。

## 7. 失败先检查

- ECHO 是否接 PA0，TRIG 是否接 PA1。
- ECHO 是否经过分压，避免 5V 直接进入 STM32。
- TIM2 中断是否开启。
- 串口请求帧 checksum 是否正确。
- 传感器前方是否有吸声材料或过远目标导致超时。
