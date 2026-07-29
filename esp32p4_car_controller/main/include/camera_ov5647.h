#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t camera_ov5647_probe(void);
esp_err_t camera_ov5647_capture_jpeg(uint8_t **jpeg_data, size_t *jpeg_len, uint32_t timeout_ms);
void camera_ov5647_release_jpeg(uint8_t *jpeg_data);
