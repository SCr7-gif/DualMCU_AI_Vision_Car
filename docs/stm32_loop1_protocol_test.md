# STM32 正式小闭环 1：协议、心跳、状态、STOP

本闭环目标：

- STM32 默认进入正式 `App_Main_Init()` / `App_Main_Loop()`。
- USART1 使用正式帧协议。
- 支持 `CMD_HEARTBEAT`、`CMD_REQUEST_STATUS`、`CMD_STOP`。
- 电机始终默认停止。
- 3 秒没有收到有效命令或心跳时，STM32 自动保持停车并在状态 flags 中标记通信超时。

## 1. 编译前确认

Keil/MDK 的 C/C++ Define 应为：

```text
USE_HAL_DRIVER,STM32F103xB
```

不要默认启用：

```text
ENABLE_STM32_TEST_MODE
```

如需临时硬件测试，才手动加入 `ENABLE_STM32_TEST_MODE`。

## 2. 串口参数

- USART1：PA9 TX / PA10 RX。
- 波特率：115200。
- 数据位：8。
- 停止位：1。
- 校验：None。

## 3. 测试帧

### 心跳

发送：

```text
AA 55 02 7F 01 82
```

含义：

- `LEN=02`
- `CMD=7F`
- `DATA=01`
- `CHECKSUM=02+7F+01=82`

正常返回：

```text
AA 55 02 FF 01 02
```

### 请求状态

发送：

```text
AA 55 01 40 41
```

正常返回两类状态帧：

- `CMD_STATUS_BASIC = 0x80`
- `CMD_STATUS_MOTOR = 0x84`

### STOP

发送：

```text
AA 55 01 10 11
```

正常返回：

- `CMD_STATUS_MOTOR = 0x84`
- motor state、target speed、current speed 都为 0。

## 4. 通信超时测试

步骤：

1. 上电后先发送心跳，确认有 ACK。
2. 停止发送任何帧超过 3 秒。
3. 再发送 `CMD_REQUEST_STATUS`。

正常现象：

- STM32 电机保持停止。
- `CMD_STATUS_BASIC` 的 flags 包含 `0x02`，表示通信超时曾触发。
- 再次收到有效命令后，通信超时 flag 清除。

## 5. 失败先检查

- PA9/PA10 是否和 USB-TTL 或 ESP32 UART 交叉连接。
- GND 是否共地。
- 是否误启用了 `ENABLE_STM32_TEST_MODE`。
- Keil 工程是否包含 `User/App` 和 `User/Protocol` 源文件。
- 帧校验是否按 `LEN + CMD + DATA` 低 8 位计算。

