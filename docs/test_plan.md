# 分阶段测试方法

## 1. STM32 协议闭环

编译：

- 使用 Keil/MDK 打开 `stm32_motion_controller/MDK-ARM/STM32_Car.uvprojx`。
- 先不启用 `ENABLE_STM32_TEST_MODE`。
- 编译正式工程。

烧录：

- ST-Link 连接 STM32。
- 下载到 STM32F103C8T6。

测试：

- ESP32 或 PC 串口发送心跳帧。
- 观察 STM32 返回 ACK。

正常现象：

- 电机不上电转动。
- 串口能收到 ACK。

失败先检查：

- USART1 PA9/PA10 是否交叉连接。
- GND 是否共地。
- 波特率是否 115200。
- 帧 checksum 是否正确。

## 2. STM32 电机 ramp

测试顺序：

1. 发送 STOP。
2. 发送低速前进，speed=20，duration=500 ms。
3. 观察 PWM 是否平滑增加。
4. 发送 STOP。

正常现象：

- 电机从停止平滑启动。
- STOP 立即停止。

失败先检查：

- TB6612 STBY 是否拉高。
- PA6/PA7 PWM 是否输出。
- PB12/PB13/PB14/PB15 方向是否接反。

## 3. STM32 DHT11

测试：

- 每 2 秒请求一次传感器状态。
- 插上 DHT11 应返回温湿度。
- 拔掉 DHT11 应返回失败，不应卡死。

正常现象：

- `dht_ok=1` 时温湿度有效。
- 读取失败时主循环仍响应心跳和 STOP。

失败先检查：

- PB5 是否接了 DHT11 DATA。
- 是否有上拉。
- 电源是否为 3.3 V 或模块支持 3.3 V。

## 4. STM32 云台

测试顺序：

1. 回中。
2. look_left_soft。
3. look_right_soft。
4. nod_yes。
5. 下发 Tilt=0。
6. 下发 Tilt=180。

正常现象：

- 动作平滑。
- Tilt 不低于 30，不高于 150。

失败先检查：

- TIM4 周期是否 20 ms。
- PB6/PB7 是否接 Pan/Tilt。
- 舵机供电是否足够。

## 5. ESP32 Web 调试

编译：

```powershell
cd esp32p4_car_controller
idf.py build
```

烧录：

```powershell
idf.py -p COMx flash monitor
```

测试：

- 手机连接同一网络。
- 浏览器打开 ESP32 打印的 IP。
- 查看 STM32、传感器、音频、摄像头、Qwen 状态。
- 点击急停。

正常现象：

- 页面能打开。
- 状态定期刷新。
- 急停会让 STM32 停车。

失败先检查：

- Wi-Fi 配置是否写入 NVS 或本地配置。
- 端口是否被防火墙拦截。
- STM32 UART 是否在线。

## 6. Qwen 普通语音聊天

测试：

- 只说普通聊天问题。
- 观察日志是否只上传音频。
- 播放回复时麦克风上传暂停。

正常现象：

- 没有图片上传日志。
- 喇叭播放回复时不会把自身声音再次上传。

失败先检查：

- API Key 是否通过 secret_manager 读取。
- WebSocket 是否连通。
- ES8311 采样率是否正确。

## 7. 视觉事件

测试：

- 说“你看到了什么”。
- 观察 image_sampler 启动单张或 1 FPS 上传。

正常现象：

- 普通聊天不上传图像。
- 视觉请求才上传 JPG。

失败先检查：

- OV5647 是否初始化成功。
- JPG 压缩是否成功。
- 图片尺寸是否过大。

