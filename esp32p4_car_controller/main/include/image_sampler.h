#pragma once

/*
 * 模块：image_sampler
 * 负责什么：控制摄像头图像上传频率，默认不向 Qwen 上传图片。
 * 用到什么硬件：后续接 OV5647 MIPI CSI；当前保留事件触发和限频接口。
 * 初始化函数：image_sampler_init()。
 * 主循环调用：image_sampler_update()，后续会在里面按 1 FPS 抽帧。
 * 怎么测试：调用 image_sampler_request_once() 后日志提示等待 camera capture 接入。
 * 失败先检查：OV5647 是否 probe 成功、CSI/JPEG 驱动是否已接入。
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IMAGE_SAMPLER_REASON_NONE = 0,
    IMAGE_SAMPLER_REASON_LOOK_AROUND,
    IMAGE_SAMPLER_REASON_FIND_USER,
    IMAGE_SAMPLER_REASON_FOLLOW_USER,
    IMAGE_SAMPLER_REASON_PATROL,
} image_sampler_reason_t;

esp_err_t image_sampler_init(void);
void image_sampler_request_once(image_sampler_reason_t reason);
void image_sampler_start_stream(image_sampler_reason_t reason, uint16_t fps);
void image_sampler_stop(void);
void image_sampler_update(uint32_t now_ms);
bool image_sampler_is_active(void);
