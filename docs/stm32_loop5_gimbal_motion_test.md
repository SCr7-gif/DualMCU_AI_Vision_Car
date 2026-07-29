# STM32 小闭环 5：云台 Motion Engine 测试

## 1. 目标

让 STM32 控制 Pan/Tilt 云台自然运动，不允许直接跳角度。云台必须支持关键帧、缓动、动作中断、当前角度读取，并且 Tilt 必须限制在 30 到 150 度。

## 2. 硬件连接

| 舵机 | STM32 |
| --- | --- |
| Pan 信号 | PB6 / TIM4_CH1 |
| Tilt 信号 | PB7 / TIM4_CH2 |
| VCC | 独立 5V 舵机电源 |
| GND | 与 STM32 共地 |

注意：舵机不要直接从 STM32 3.3V 供电。

## 3. 实现策略

- `bsp_servo` 只负责把角度转换成 TIM4 PWM 脉宽。
- `easing` 提供整数缓动插值，不使用浮点和复杂库。
- `gimbal_control` 保存当前角度、关键帧队列和动作状态。
- `Gimbal_Update()` 在主循环中平滑推进动作，不阻塞。
- 新云台命令会中断当前动作并启动新的动作。
- Tilt 在进入队列前硬限制到 30 到 150 度。

## 4. 串口测试帧

请求状态：

```text
AA 55 01 40 41
```

回中，600 ms：

```text
AA 55 03 20 58 02 7D
```

Pan 到 60 度，600 ms，ease_in_out：

```text
AA 55 05 21 3C 58 02 01 BD
```

Tilt 到 20 度，600 ms，ease_in_out。期望被限制到 30 度：

```text
AA 55 05 22 14 58 02 01 96
```

Pan/Tilt 到 120/120，800 ms，smooth_step：

```text
AA 55 06 23 78 78 20 03 02 3E
```

执行 nod_yes 动作：

```text
AA 55 02 24 03 29
```

## 5. 状态帧

`CMD_STATUS_GIMBAL = 0x83`

```text
AA 55 05 83 pan tilt moving action_id checksum
```

字段：

| 字段 | 含义 |
| --- | --- |
| `pan` | 当前 Pan 角度 |
| `tilt` | 当前 Tilt 角度，范围 30 到 150 |
| `moving` | `1` 表示正在运动，`0` 表示动作完成 |
| `action_id` | 当前动作 ID |

## 6. 正常现象

- 上电后 Pan/Tilt 回到 90/90。
- 发送 set_pan/set_tilt/set_pan_tilt 后，舵机平滑移动，不瞬间跳变。
- 发送 Tilt=20 后，状态帧里的 Tilt 不低于 30。
- 动作执行中发送新云台命令，旧动作被中断，新动作平滑开始。
- 发送 STOP 后，电机停车，云台当前动作停止在当前位置。

## 7. 失败先检查

- TIM4 是否初始化并输出 PWM。
- PB6/PB7 是否接对。
- 舵机电源是否足够，是否与 STM32 共地。
- 串口帧 LEN、payload、checksum 是否正确。
- 如果 Tilt 超出范围，先看状态帧是否已被限制到 30 到 150。
