# ESP32-P4 小闭环 3：image_sampler 和相机上传测试

## 1. 目标

本轮把“视觉触发”链路接完整：

- Qwen 可以调用 `robot_capture_image`。
- Web 调试页可以点击 `Capture`。
- `robot_tools_capture_image()` 会触发 `image_sampler_request_once()`。
- `image_sampler_update()` 会调用 `camera_ov5647_capture_jpeg()`。
- 如果拿到真实 JPEG，就 base64 后通过 `input_image_buffer.append` 发给 Qwen。

注意：当前 `camera_ov5647_capture_jpeg()` 仍返回 `ESP_ERR_NOT_SUPPORTED`，因为 ESP32-P4 的 OV5647 MIPI CSI + JPEG 取帧需要按实际板卡接入 Espressif camera/video 驱动。这里没有伪造图片。

当前代码已经预留可选 V4L2 后端：

- `CONFIG_ROBOT_CAMERA_V4L2_CAPTURE`
- `CONFIG_ROBOT_CAMERA_V4L2_DEVICE`，默认 `/dev/video0`
- `CONFIG_ROBOT_CAMERA_JPEG_WIDTH`，默认 `640`
- `CONFIG_ROBOT_CAMERA_JPEG_HEIGHT`，默认 `480`

只有在 ESP-IDF 已经通过 `esp_video` 注册好 MIPI CSI 摄像头设备后，才打开这个配置。

## 2. 怎么编译

在 ESP-IDF PowerShell 中执行：

```powershell
cd E:\CodexProject\Wayne_Graduation\DualMCU_AI_Vision_Car\esp32p4_car_controller
idf.py set-target esp32p4
idf.py build
```

## 3. 怎么烧录

```powershell
idf.py -p COMx flash monitor
```

把 `COMx` 换成设备管理器里的 ESP32-P4 串口。

## 4. 怎么测试

1. 先确认 Qwen 已连接，日志出现：

```text
qwen_realtime: connected
qwen_realtime: session updated
```

2. 打开 Web 调试页：

```text
http://ESP32_IP/
```

3. 点击 Qwen 区域的 `Capture`。

4. 或者在 Qwen 文本框输入：

```text
拍一张照片看看
```

如果模型调用工具，日志会出现：

```text
function call: robot_capture_image args=...
tool robot_capture_image
```

## 5. 当前正常现象

在真实相机取帧驱动还没接入前，点击 `Capture` 后正常会看到：

```text
ov5647: OV5647 JPEG capture is not wired to the ESP32-P4 CSI/JPEG driver yet
image_sampler: image capture failed reason=1 ret=ESP_ERR_NOT_SUPPORTED
```

这说明上层触发链路是通的，但底层相机取帧还没完成。

## 6. 接入真实取帧后的正常现象

当 `camera_ov5647_capture_jpeg()` 被替换成真实驱动实现后，日志应看到：

```text
image_sampler: image upload reason=1 len=xxxxx ret=ESP_OK
```

如果启用了 V4L2 后端，正常路径是：

```text
/dev/video0 -> V4L2_PIX_FMT_JPEG -> image_sampler -> input_image_buffer.append
```

Qwen 应能基于上传的图片回答问题。

## 7. 失败先检查什么

1. `camera_ov5647_probe()` 是否能读到 `0x5647`。
2. OV5647 的 RESET/PWDN/I2C/MIPI CSI 连接是否和 `board_config.h` 一致。
3. Qwen 是否已连接，`qwen_state` 是否进入 connected/session ready。
4. ESP32-P4 是否有真实网络，不是仅启动了本地 TCP/IP 栈。
5. JPEG 缓冲是否过大导致内存不足。

## 8. 下一步

下一步要按实际板卡完成 `esp_video` 的 OV5647 MIPI CSI 初始化，让 `/dev/video0` 真正出现并能输出 JPEG。
