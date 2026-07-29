#include "camera_ov5647.h"

#include "board_config.h"
#include "board_i2c.h"
#include "esp_log.h"
#include "robot_camera.h"
#include "sdkconfig.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_ROBOT_CAMERA_V4L2_CAPTURE
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "linux/videodev2.h"
#endif

static const char *TAG = "ov5647";

#if CONFIG_ROBOT_CAMERA_V4L2_CAPTURE
#define CAMERA_V4L2_BUFFER_COUNT 2

static esp_err_t camera_capture_v4l2_jpeg(uint8_t **jpeg_data, size_t *jpeg_len, uint32_t timeout_ms)
{
    int fd = -1;
    uint8_t *mapped[CAMERA_V4L2_BUFFER_COUNT] = {0};
    size_t mapped_len[CAMERA_V4L2_BUFFER_COUNT] = {0};
    bool stream_on = false;
    esp_err_t ret = ESP_FAIL;
    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    (void)timeout_ms;

    fd = open(CONFIG_ROBOT_CAMERA_V4L2_DEVICE, O_RDONLY);
    if (fd < 0) {
        ESP_LOGW(TAG, "open %s failed errno=%d", CONFIG_ROBOT_CAMERA_V4L2_DEVICE, errno);
        return ESP_FAIL;
    }

    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .fmt.pix.width = CONFIG_ROBOT_CAMERA_JPEG_WIDTH,
        .fmt.pix.height = CONFIG_ROBOT_CAMERA_JPEG_HEIGHT,
        .fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG,
    };
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) != 0) {
        ESP_LOGW(TAG, "VIDIOC_S_FMT JPEG %dx%d failed errno=%d",
                 CONFIG_ROBOT_CAMERA_JPEG_WIDTH, CONFIG_ROBOT_CAMERA_JPEG_HEIGHT, errno);
        goto cleanup;
    }

    struct v4l2_requestbuffers req = {
        .count = CAMERA_V4L2_BUFFER_COUNT,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGW(TAG, "VIDIOC_REQBUFS failed errno=%d", errno);
        goto cleanup;
    }

    for (uint32_t i = 0; i < req.count && i < CAMERA_V4L2_BUFFER_COUNT; i++) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i,
        };
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGW(TAG, "VIDIOC_QUERYBUF failed errno=%d", errno);
            goto cleanup;
        }

        mapped[i] = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (mapped[i] == MAP_FAILED) {
            mapped[i] = NULL;
            ESP_LOGW(TAG, "mmap failed errno=%d", errno);
            goto cleanup;
        }
        mapped_len[i] = buf.length;

        if (ioctl(fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGW(TAG, "VIDIOC_QBUF failed errno=%d", errno);
            goto cleanup;
        }
    }

    if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGW(TAG, "VIDIOC_STREAMON failed errno=%d", errno);
        goto cleanup;
    }
    stream_on = true;

    struct v4l2_buffer frame = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(fd, VIDIOC_DQBUF, &frame) != 0) {
        ESP_LOGW(TAG, "VIDIOC_DQBUF failed errno=%d", errno);
        goto cleanup;
    }

    if ((frame.index >= CAMERA_V4L2_BUFFER_COUNT) || (mapped[frame.index] == NULL) || (frame.bytesused == 0)) {
        ESP_LOGW(TAG, "bad jpeg frame index=%u bytes=%u", (unsigned)frame.index, (unsigned)frame.bytesused);
        goto cleanup;
    }

    *jpeg_data = malloc(frame.bytesused);
    if (*jpeg_data == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    memcpy(*jpeg_data, mapped[frame.index], frame.bytesused);
    *jpeg_len = frame.bytesused;
    ret = ESP_OK;

cleanup:
    if (stream_on) {
        ioctl(fd, VIDIOC_STREAMOFF, &type);
    }
    for (size_t i = 0; i < CAMERA_V4L2_BUFFER_COUNT; i++) {
        if (mapped[i] != NULL) {
            munmap(mapped[i], mapped_len[i]);
        }
    }
    if (fd >= 0) {
        close(fd);
    }
    if (ret != ESP_OK) {
        free(*jpeg_data);
        *jpeg_data = NULL;
        *jpeg_len = 0;
    }
    return ret;
}
#endif

esp_err_t camera_ov5647_probe(void)
{
    uint8_t id_high = 0;
    uint8_t id_low = 0;

    ESP_LOGI(TAG, "OV5647 reset_gpio=%d pwdn_gpio=%d", BOARD_OV5647_RESET_GPIO, BOARD_OV5647_PWDN_GPIO);
    ESP_LOGI(TAG, "MIPI CSI D0/D1/CLK are dedicated signals, not normal GPIO");

    esp_err_t ret1 = board_i2c_read_reg16(BOARD_OV5647_I2C_ADDR, 0x300A, &id_high);
    esp_err_t ret2 = board_i2c_read_reg16(BOARD_OV5647_I2C_ADDR, 0x300B, &id_low);
    if (ret1 != ESP_OK || ret2 != ESP_OK) {
        ESP_LOGW(TAG, "OV5647 probe failed at I2C addr 0x%02X, ret=%s/%s",
                 BOARD_OV5647_I2C_ADDR, esp_err_to_name(ret1), esp_err_to_name(ret2));
        return ret1 != ESP_OK ? ret1 : ret2;
    }

    ESP_LOGI(TAG, "OV5647 chip id: 0x%02X%02X", id_high, id_low);
    if (id_high == 0x56 && id_low == 0x47) {
        ESP_LOGI(TAG, "OV5647 I2C init/probe OK");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "unexpected camera id, expected 0x5647");
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t camera_ov5647_capture_jpeg(uint8_t **jpeg_data, size_t *jpeg_len, uint32_t timeout_ms)
{
    if ((jpeg_data == NULL) || (jpeg_len == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    *jpeg_data = NULL;
    *jpeg_len = 0;

#if CONFIG_ROBOT_CAMERA_V4L2_CAPTURE
    return robot_camera_capture_jpeg(jpeg_data, jpeg_len, timeout_ms);
#else
    static bool logged_once;
    /*
     * The project has the OV5647 probe and the image upload pipeline, but the
     * board-specific ESP32-P4 MIPI CSI + JPEG frame capture still needs to be
     * bound to Espressif's camera/video driver for the exact board routing.
     */
    if (!logged_once) {
        ESP_LOGW(TAG, "OV5647 JPEG capture is not wired to the ESP32-P4 CSI/JPEG driver yet");
        logged_once = true;
    }
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void camera_ov5647_release_jpeg(uint8_t *jpeg_data)
{
    robot_camera_release_jpeg(jpeg_data);
}
