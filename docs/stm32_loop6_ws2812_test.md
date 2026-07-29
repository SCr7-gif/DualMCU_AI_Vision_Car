# STM32 小闭环 6：WS2812 状态灯测试

## 1. 目标

让 STM32 通过 PA8 控制 WS2812 灯带，支持直接设置 RGB 和常用状态灯效。灯效更新必须非阻塞，不影响串口、急停、电机、云台和传感器。

## 2. 硬件连接

| WS2812 | STM32 |
| --- | --- |
| DIN | PA8 / TIM1_CH1 |
| VCC | 5V |
| GND | GND |

注意：WS2812 需要 5V 供电，必须和 STM32 共地。灯带较长时需要单独供电，不能从 STM32 取电。

## 3. 实现策略

- TIM1_CH1 输出 1.25 us 周期 PWM。
- DMA1_Channel2 发送 WS2812 高低码脉宽。
- 默认支持 8 颗灯，GRB 顺序发送。
- 主循环调用 `BspWs2812_Update()`，每 50 ms 刷新一次灯效。
- DMA 忙时跳过本次刷新，不阻塞主循环。
- 支持 idle、listening、speaking、thinking、error、cute、patrol、solid。

## 4. 串口测试帧

设置纯红：

```text
AA 55 04 30 20 00 00 54
```

设置纯绿：

```text
AA 55 04 30 00 20 00 54
```

设置纯蓝：

```text
AA 55 04 30 00 00 20 54
```

listening 绿色呼吸，持续执行：

```text
AA 55 07 31 01 00 20 00 00 00 59
```

error 红色闪烁，持续 3000 ms：

```text
AA 55 07 31 04 20 00 00 B8 0B 1F
```

cute 粉色/蓝色交替，持续 5000 ms：

```text
AA 55 07 31 05 20 00 18 88 13 10
```

patrol 蓝色跑点，持续执行：

```text
AA 55 07 31 06 00 00 20 00 00 5E
```

## 5. 正常现象

- 上电后默认 idle，灯带显示低亮蓝色。
- `CMD_LED_RGB` 会立即变成指定颜色。
- `CMD_LED_EFFECT` 会显示对应动态效果。
- 带 duration 的灯效到期后自动回到 idle。
- 灯效运行时，串口心跳、STOP、电机 ramp、云台动作、传感器状态仍正常响应。

## 6. 失败先检查

- PA8 是否接到 WS2812 DIN。
- WS2812 是否 5V 供电，GND 是否和 STM32 共地。
- 第一颗灯方向是否接反，DIN/DOUT 是否接错。
- 如果灯颜色错位，检查 WS2812 型号是否为 GRB 顺序。
- 如果完全不亮，检查 TIM1 DMA 和 DMA1_Channel2 中断是否启用。
- 串口帧 checksum 是否正确。
