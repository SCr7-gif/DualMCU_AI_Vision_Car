#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*robot_camera_slow_mode_fn_t)(void);

typedef struct {
    bool initialized;
    bool ready;
    char device[24];
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t pixfmt;
    uint32_t last_frame_ms;
    uint32_t frames;
    uint32_t dropped_frames;
    uint32_t stream_clients;
    esp_err_t last_error;
} robot_camera_status_t;

esp_err_t robot_camera_init(void);
void robot_camera_get_status(robot_camera_status_t *out);
esp_err_t robot_camera_capture_jpeg(uint8_t **jpeg_data, size_t *jpeg_len, uint32_t timeout_ms);
void robot_camera_release_jpeg(uint8_t *jpeg_data);
esp_err_t robot_camera_stream_mjpeg(httpd_req_t *req, robot_camera_slow_mode_fn_t slow_mode_fn);
const char *robot_camera_fourcc_string(uint32_t pixfmt, char out[5]);
