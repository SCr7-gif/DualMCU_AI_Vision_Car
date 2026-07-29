# ESP32-P4 小闭环 4：串口 monitor 控制测试

补充：当前 monitor 支持 `diag` 命令，会输出完整 JSON 诊断，包括 target、network、secrets、qwen、camera、stm32_uart、i2c 和 image_sampler 状态。

## 1. 目标

给 ESP32-P4 增加一个不依赖 Web、不依赖 Qwen 的串口控制入口。烧录后打开 `idf.py monitor`，可以直接输入命令测试 STM32、云台、灯效、行为和 Capture 链路。

## 2. 怎么编译

在 ESP-IDF PowerShell：

```powershell
cd E:\CodexProject\Wayne_Graduation\DualMCU_AI_Vision_Car\esp32p4_car_controller
idf.py set-target esp32p4
idf.py build
```

## 3. 怎么烧录

```powershell
idf.py -p COMx flash monitor
```

把 `COMx` 换成 ESP32-P4 的实际串口。

## 4. 怎么接线

ESP32-P4 和 STM32 必须共地：

- ESP32-P4 `GPIO47 TX` -> STM32 `PA10 RX`
- ESP32-P4 `GPIO48 RX` <- STM32 `PA9 TX`
- ESP32-P4 `GND` <-> STM32 `GND`

STM32 USART1 是 `115200 8N1`。

## 5. 怎么测试

进入 monitor 后会看到：

```text
car>
```

先输入：

```text
status
```

再按顺序测试：

```text
stop
diag
net
led cute
gimbal 90 90 700
gimbal_action scan_slow
behavior cute
move forward 25 600
capture
qwenctl status
qwenctl start
qwen 你好
```

测试电机前，建议先把车轮架空。

## 6. 正常现象

- `status` 能打印 `stm32=1`，说明 ESP32 收到 STM32 心跳或状态帧。
- `net` 能打印 `started/connected/native_wifi/qwen_state`，用于判断当前 ESP32-P4 网络是否真的可用。
- `stop` 返回 `ESP_OK`，电机保持停止。
- `led cute` 后 WS2812 切换灯效。
- `gimbal_action scan_slow` 后云台缓慢扫描。
- `behavior cute` 后 STM32 执行本地 cute 行为。
- `move forward 25 600` 后轮子低速短暂前进，然后停止或进入安全限制。
- `capture` 当前正常会返回上层触发成功，随后日志提示相机 JPEG 取帧 `ESP_ERR_NOT_SUPPORTED`，表示真实 MIPI CSI/JPEG 驱动还没接。

## 7. 失败先检查什么

1. `stm32=0`：先查 GPIO47/48 和 PA9/PA10 是否交叉接线，GND 是否共地。
2. 命令返回错误：查 STM32 是否已经烧录正式程序，不是 TestOnly。
3. 电机不转：先查超声波是否一直小于 15 cm，再查电机驱动供电和使能。
4. 云台不动：查 TIM4 PB6/PB7 舵机供电和 GND。
5. `capture` 报 `ESP_ERR_NOT_SUPPORTED`：这是当前正常现象，下一步接真实相机驱动。

## 8. 下一步

用串口命令先把 STM32 控制链路测稳定，再继续接 ESP32-P4 的真实网络和 OV5647 MIPI CSI/JPEG 取帧。
