# STM32 小闭环 7：GPS NMEA 接收测试

## 1. 目标

让 STM32 通过 USART2 非阻塞接收 GPS 模块输出的 NMEA 文本，缓存最近一行，并通过 `CMD_STATUS_GPS = 0x82` 上报给 ESP32-P4。

## 2. 硬件连接

| GPS 模块 | STM32 |
| --- | --- |
| TX | PA3 / USART2_RX |
| RX | PA2 / USART2_TX，可不接 |
| VCC | 按模块要求，常见为 3.3V 或 5V |
| GND | GND |

注意：GPS TX 必须是 STM32 可接受的 3.3V 电平。模块需要天线能看到天空，室内可能长时间无定位，但通常仍会输出 NMEA。

## 3. 实现策略

- 不改 CubeMX 自动生成 USART2 配置。
- USART2 使用已有 9600 8N1。
- `BspGps_Update()` 在主循环中轮询 RXNE，每次最多处理 32 字节。
- 收到 `$` 开始缓存一行，收到 `\n` 结束一行。
- 缓存最近一条以 `$` 开头的 NMEA 文本，去掉 `\r\n`。
- `CMD_STATUS_GPS` 直接上报 ASCII NMEA；没收到完整行时 payload 为空。

## 4. 串口测试

请求状态：

```text
AA 55 01 40 41
```

期望 STM32 返回：

- `0x80` 基础状态
- `0x84` 电机状态
- `0x83` 云台状态
- `0x81` 传感器状态
- `0x82` GPS 状态

`0x82` 示例 payload：

```text
$GPRMC,072414.00,A,3150.1234,N,11711.5678,E,0.03,0.00,260626,,,A*68
```

如果还没有收到完整 NMEA 行，`0x82` 可能是空 payload：

```text
AA 55 01 82 83
```

## 5. 正常现象

- GPS 模块输出 NMEA 后，请求状态能看到 `$GPRMC`、`$GPGGA`、`$GNRMC` 或 `$GNGGA` 等文本。
- 没定位时，NMEA 仍可能存在，但字段里会显示无效定位状态。
- GPS 接收不影响串口心跳、STOP、电机、云台、灯效和传感器读取。

## 6. 失败先检查

- GPS TX 是否接 STM32 PA3。
- GPS 和 STM32 是否共地。
- GPS 波特率是否为 9600。
- GPS TX 电平是否为 3.3V。
- 是否等待了足够时间，天线是否能看到天空。
- 如果 `0x82` 一直为空，用 USB-TTL 单独接 GPS TX 检查是否有 NMEA 输出。
