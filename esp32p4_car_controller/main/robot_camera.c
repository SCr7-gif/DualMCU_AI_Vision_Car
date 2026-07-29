#include "robot_camera.h"

#include "board_i2c.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "driver/jpeg_encode.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include "sdkconfig.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static const char *TAG = "robot_camera";

#define CAMERA_BUFFER_COUNT 2
#define CAMERA_MAX_STREAM_CLIENTS 1
#define CAMERA_I2C_FREQ_HZ 400000
#define CAMERA_RESET_GPIO GPIO_NUM_NC
#define CAMERA_PWDN_GPIO GPIO_NUM_NC

typedef struct {
    uint8_t *ptr;
    size_t len;
} camera_mmap_buffer_t;

static SemaphoreHandle_t s_lock;
static bool s_initialized;
static bool s_ready;
static int s_fd = -1;
static camera_mmap_buffer_t s_buffers[CAMERA_BUFFER_COUNT];
static uint32_t s_buffer_count;
static uint32_t s_width;
static uint32_t s_height;
static uint32_t s_pixfmt;
static uint32_t s_fps;
static uint32_t s_frames;
static uint32_t s_dropped_frames;
static uint32_t s_stream_clients;
static int64_t s_last_frame_time_us;
static esp_err_t s_last_error = ESP_ERR_INVALID_STATE;
static uint8_t *s_latest_jpeg;
static size_t s_latest_jpeg_len;
static bool s_source_is_jpeg;
static jpeg_encoder_handle_t s_jpeg_encoder;
static jpeg_encode_cfg_t s_jpeg_cfg;
static uint8_t *s_jpeg_out_buf;
static uint32_t s_jpeg_out_size;
static uint8_t *s_rotate_buf;
static uint32_t s_rotate_buf_size;

static void set_last_error(esp_err_t err)
{
    s_last_error = err;
}

const char *robot_camera_fourcc_string(uint32_t pixfmt, char out[5])
{
    out[0] = (char)(pixfmt & 0xff);
    out[1] = (char)((pixfmt >> 8) & 0xff);
    out[2] = (char)((pixfmt >> 16) & 0xff);
    out[3] = (char)((pixfmt >> 24) & 0xff);
    out[4] = '\0';
    return out;
}

static esp_err_t update_latest_locked(const uint8_t *data, size_t len)
{
    uint8_t *copy = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (copy == NULL) {
        copy = malloc(len);
    }
    if (copy == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(copy, data, len);
    free(s_latest_jpeg);
    s_latest_jpeg = copy;
    s_latest_jpeg_len = len;
    return ESP_OK;
}

static esp_err_t init_jpeg_encoder_locked(void)
{
    if (s_source_is_jpeg || s_jpeg_encoder != NULL) {
        return ESP_OK;
    }

    jpeg_encode_engine_cfg_t eng_cfg = {
        .timeout_ms = 1500,
    };
    ESP_RETURN_ON_ERROR(jpeg_new_encoder_engine(&eng_cfg, &s_jpeg_encoder), TAG, "jpeg_new_encoder_engine failed");

    memset(&s_jpeg_cfg, 0, sizeof(s_jpeg_cfg));
    s_jpeg_cfg.width = s_width;
    s_jpeg_cfg.height = s_height;
    s_jpeg_cfg.image_quality = CONFIG_ROBOT_CAMERA_JPEG_QUALITY;
    if (s_pixfmt == V4L2_PIX_FMT_RGB565) {
        s_jpeg_cfg.src_type = JPEG_ENCODE_IN_FORMAT_RGB565;
        s_jpeg_cfg.sub_sample = JPEG_DOWN_SAMPLING_YUV422;
    } else {
        ESP_LOGE(TAG, "unsupported encoder input format 0x%08lx", (unsigned long)s_pixfmt);
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_jpeg_out_size = s_width * s_height * 2;
    size_t actual_size = 0;
    jpeg_encode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    s_jpeg_out_buf = jpeg_alloc_encoder_mem(s_jpeg_out_size, &mem_cfg, &actual_size);
    s_jpeg_out_size = (uint32_t)actual_size;
    ESP_RETURN_ON_FALSE(s_jpeg_out_buf != NULL, ESP_ERR_NO_MEM, TAG, "jpeg output buffer alloc failed");

    s_rotate_buf_size = s_width * s_height * 2;
    s_rotate_buf = heap_caps_malloc(s_rotate_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_rotate_buf == NULL) {
        s_rotate_buf = malloc(s_rotate_buf_size);
    }
    ESP_RETURN_ON_FALSE(s_rotate_buf != NULL, ESP_ERR_NO_MEM, TAG, "rotate buffer alloc failed");

    ESP_LOGI(TAG, "JPEG encoder ready input=RGB565 %lux%lu quality=%d out_buf=%lu",
             (unsigned long)s_width,
             (unsigned long)s_height,
             CONFIG_ROBOT_CAMERA_JPEG_QUALITY,
             (unsigned long)s_jpeg_out_size);
    return ESP_OK;
}

static const uint8_t *rotate_rgb565_180_locked(const uint8_t *src, size_t src_len)
{
    size_t expected = (size_t)s_width * (size_t)s_height * 2u;
    if ((src_len < expected) || (s_rotate_buf == NULL) || (s_rotate_buf_size < expected)) {
        return src;
    }

    const uint8_t *from = src + expected - 2;
    uint8_t *to = s_rotate_buf;
    for (size_t i = 0; i < expected; i += 2) {
        to[i] = from[0];
        to[i + 1] = from[1];
        from -= 2;
    }
    return s_rotate_buf;
}

static esp_err_t encode_frame_locked(const uint8_t *src, size_t src_len, const uint8_t **jpeg, size_t *jpeg_len)
{
    if (s_source_is_jpeg) {
        *jpeg = src;
        *jpeg_len = src_len;
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(init_jpeg_encoder_locked(), TAG, "jpeg encoder init failed");
    const uint8_t *rotated_src = rotate_rgb565_180_locked(src, src_len);
    uint32_t out_len = 0;
    ESP_RETURN_ON_ERROR(jpeg_encoder_process(s_jpeg_encoder,
                                             &s_jpeg_cfg,
                                             rotated_src,
                                             (uint32_t)src_len,
                                             s_jpeg_out_buf,
                                             s_jpeg_out_size,
                                             &out_len),
                        TAG,
                        "jpeg encode failed");
    *jpeg = s_jpeg_out_buf;
    *jpeg_len = out_len;
    return ESP_OK;
}

static esp_err_t copy_latest_locked(uint8_t **jpeg_data, size_t *jpeg_len)
{
    if ((s_latest_jpeg == NULL) || (s_latest_jpeg_len == 0)) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t *copy = malloc(s_latest_jpeg_len);
    if (copy == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(copy, s_latest_jpeg, s_latest_jpeg_len);
    *jpeg_data = copy;
    *jpeg_len = s_latest_jpeg_len;
    return ESP_OK;
}

static esp_err_t try_set_jpeg_format(int fd, uint32_t width, uint32_t height)
{
    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .fmt.pix.width = width,
        .fmt.pix.height = height,
        .fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG,
        .fmt.pix.field = V4L2_FIELD_NONE,
    };

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) != 0) {
        ESP_LOGW(TAG, "VIDIOC_S_FMT JPEG %lux%lu failed errno=%d", (unsigned long)width, (unsigned long)height, errno);
        return ESP_FAIL;
    }

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG) {
        char fourcc[5];
        ESP_LOGW(TAG, "camera returned non-JPEG format %s for %lux%lu",
                 robot_camera_fourcc_string(fmt.fmt.pix.pixelformat, fourcc),
                 (unsigned long)fmt.fmt.pix.width,
                 (unsigned long)fmt.fmt.pix.height);
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_width = fmt.fmt.pix.width;
    s_height = fmt.fmt.pix.height;
    s_pixfmt = fmt.fmt.pix.pixelformat;
    s_source_is_jpeg = true;
    return ESP_OK;
}

static esp_err_t use_current_rgb565_format(int fd)
{
    struct v4l2_format got = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };
    if (ioctl(fd, VIDIOC_G_FMT, &got) != 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed errno=%d", errno);
        return ESP_FAIL;
    }

    char fourcc[5] = {0};
    ESP_LOGW(TAG, "%s does not provide JPEG directly; got %s %lux%lu",
             CONFIG_ROBOT_CAMERA_V4L2_DEVICE,
             robot_camera_fourcc_string(got.fmt.pix.pixelformat, fourcc),
             (unsigned long)got.fmt.pix.width,
             (unsigned long)got.fmt.pix.height);

    if (got.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB565X) {
        got.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
        if (ioctl(fd, VIDIOC_S_FMT, &got) != 0) {
            ESP_LOGW(TAG, "VIDIOC_S_FMT RGB565 failed errno=%d", errno);
        } else {
            ESP_LOGI(TAG, "switched camera output to RGB565");
        }
    }

    if (got.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB565) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_width = got.fmt.pix.width;
    s_height = got.fmt.pix.height;
    s_pixfmt = got.fmt.pix.pixelformat;
    s_source_is_jpeg = false;
    return ESP_OK;
}

static void configure_fps(int fd, uint32_t fps)
{
    struct v4l2_streamparm parm = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps;
    if (ioctl(fd, VIDIOC_S_PARM, &parm) == 0) {
        s_fps = fps;
    } else {
        ESP_LOGW(TAG, "VIDIOC_S_PARM %lu fps failed errno=%d", (unsigned long)fps, errno);
        s_fps = fps;
    }
}

static esp_err_t setup_buffers(int fd)
{
    struct v4l2_requestbuffers req = {
        .count = CAMERA_BUFFER_COUNT,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(fd, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed errno=%d", errno);
        return ESP_FAIL;
    }

    if (req.count == 0) {
        ESP_LOGE(TAG, "camera returned zero buffers");
        return ESP_FAIL;
    }

    s_buffer_count = req.count > CAMERA_BUFFER_COUNT ? CAMERA_BUFFER_COUNT : req.count;
    for (uint32_t i = 0; i < s_buffer_count; i++) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i,
        };
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF[%lu] failed errno=%d", (unsigned long)i, errno);
            return ESP_FAIL;
        }

        s_buffers[i].ptr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (s_buffers[i].ptr == MAP_FAILED) {
            s_buffers[i].ptr = NULL;
            ESP_LOGE(TAG, "mmap[%lu] failed errno=%d", (unsigned long)i, errno);
            return ESP_FAIL;
        }
        s_buffers[i].len = buf.length;

        if (ioctl(fd, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF[%lu] failed errno=%d", (unsigned long)i, errno);
            return ESP_FAIL;
        }
    }

    const int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed errno=%d", errno);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t dequeue_frame_locked(const uint8_t **data, size_t *len)
{
    struct v4l2_buffer frame = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };

    if (ioctl(s_fd, VIDIOC_DQBUF, &frame) != 0) {
        s_dropped_frames++;
        ESP_LOGW(TAG, "VIDIOC_DQBUF failed errno=%d", errno);
        return ESP_FAIL;
    }

    if ((frame.index >= s_buffer_count) || (s_buffers[frame.index].ptr == NULL) || (frame.bytesused == 0)) {
        s_dropped_frames++;
        (void)ioctl(s_fd, VIDIOC_QBUF, &frame);
        ESP_LOGW(TAG, "bad frame index=%lu bytes=%lu",
                 (unsigned long)frame.index,
                 (unsigned long)frame.bytesused);
        return ESP_FAIL;
    }

    const uint8_t *encoded = NULL;
    size_t encoded_len = 0;
    esp_err_t ret = encode_frame_locked(s_buffers[frame.index].ptr, frame.bytesused, &encoded, &encoded_len);
    if (ret == ESP_OK) {
        ret = update_latest_locked(encoded, encoded_len);
    }
    *data = encoded;
    *len = encoded_len;
    s_frames++;
    s_last_frame_time_us = esp_timer_get_time();

    if (ioctl(s_fd, VIDIOC_QBUF, &frame) != 0) {
        s_dropped_frames++;
        ESP_LOGW(TAG, "VIDIOC_QBUF after frame failed errno=%d", errno);
    }
    return ret;
}

esp_err_t robot_camera_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_initialized) {
        esp_err_t ret = s_ready ? ESP_OK : s_last_error;
        xSemaphoreGive(s_lock);
        return ret;
    }
    s_initialized = true;
    xSemaphoreGive(s_lock);

    esp_err_t ret = ESP_OK;
    i2c_master_bus_handle_t i2c_bus = board_i2c_get_bus_handle();
    if (i2c_bus == NULL) {
        ret = board_i2c_init();
        if (ret != ESP_OK) {
            set_last_error(ret);
            return ret;
        }
        i2c_bus = board_i2c_get_bus_handle();
    }

    esp_video_init_csi_config_t csi_config = {
        .sccb_config = {
            .init_sccb = false,
            .i2c_handle = i2c_bus,
            .freq = CAMERA_I2C_FREQ_HZ,
        },
        .reset_pin = CAMERA_RESET_GPIO,
        .pwdn_pin = CAMERA_PWDN_GPIO,
    };
    esp_video_init_config_t video_config = {
        .csi = &csi_config,
    };

    ret = esp_video_init_with_flags(&video_config,
                                    ESP_VIDEO_INIT_FLAGS_MIPI_CSI |
                                        ESP_VIDEO_INIT_FLAGS_ISP |
                                        ESP_VIDEO_INIT_FLAGS_JPEG);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_video init failed: %s", esp_err_to_name(ret));
        set_last_error(ret);
        return ret;
    }
    ESP_LOGI(TAG, "esp_video init OK");

    int fd = open(CONFIG_ROBOT_CAMERA_V4L2_DEVICE, O_RDWR);
    if (fd < 0) {
        ESP_LOGE(TAG, "open %s failed errno=%d", CONFIG_ROBOT_CAMERA_V4L2_DEVICE, errno);
        set_last_error(ESP_FAIL);
        return ESP_FAIL;
    }

    ret = try_set_jpeg_format(fd, CONFIG_ROBOT_CAMERA_JPEG_WIDTH, CONFIG_ROBOT_CAMERA_JPEG_HEIGHT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "fallback to 640x480 JPEG");
        ret = try_set_jpeg_format(fd, 640, 480);
    }
    if (ret != ESP_OK) {
        ret = use_current_rgb565_format(fd);
        if (ret != ESP_OK) {
            close(fd);
            set_last_error(ret);
            return ret;
        }
    }

    if (!s_source_is_jpeg) {
        ret = init_jpeg_encoder_locked();
        if (ret != ESP_OK) {
            close(fd);
            set_last_error(ret);
            return ret;
        }
    }

    configure_fps(fd, CONFIG_ROBOT_CAMERA_STREAM_FPS);
    ret = setup_buffers(fd);
    if (ret != ESP_OK) {
        close(fd);
        set_last_error(ret);
        return ret;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_fd = fd;
    s_ready = true;
    s_last_error = ESP_OK;
    xSemaphoreGive(s_lock);

    char fourcc[5];
    ESP_LOGI(TAG, "%s ready %lux%lu %s fps=%lu",
             CONFIG_ROBOT_CAMERA_V4L2_DEVICE,
             (unsigned long)s_width,
             (unsigned long)s_height,
             s_source_is_jpeg ? "JPEG" : robot_camera_fourcc_string(s_pixfmt, fourcc),
             (unsigned long)s_fps);
    return ESP_OK;
}

void robot_camera_get_status(robot_camera_status_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    out->initialized = s_initialized;
    out->ready = s_ready;
    snprintf(out->device, sizeof(out->device), "%s", CONFIG_ROBOT_CAMERA_V4L2_DEVICE);
    out->width = s_width;
    out->height = s_height;
    out->fps = s_fps;
    out->pixfmt = s_pixfmt;
    out->frames = s_frames;
    out->dropped_frames = s_dropped_frames;
    out->stream_clients = s_stream_clients;
    out->last_error = s_last_error;
    if (s_last_frame_time_us > 0) {
        out->last_frame_ms = (uint32_t)((esp_timer_get_time() - s_last_frame_time_us) / 1000);
    }
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

esp_err_t robot_camera_capture_jpeg(uint8_t **jpeg_data, size_t *jpeg_len, uint32_t timeout_ms)
{
    if ((jpeg_data == NULL) || (jpeg_len == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    *jpeg_data = NULL;
    *jpeg_len = 0;

    esp_err_t ret = robot_camera_init();
    if (ret != ESP_OK) {
        return ret;
    }

    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms == 0 ? 1000 : timeout_ms);
    if (xSemaphoreTake(s_lock, timeout_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const uint8_t *frame = NULL;
    size_t frame_len = 0;
    ret = dequeue_frame_locked(&frame, &frame_len);
    if (ret == ESP_OK) {
        ret = copy_latest_locked(jpeg_data, jpeg_len);
    } else {
        ret = copy_latest_locked(jpeg_data, jpeg_len);
    }
    xSemaphoreGive(s_lock);
    return ret;
}

void robot_camera_release_jpeg(uint8_t *jpeg_data)
{
    free(jpeg_data);
}

esp_err_t robot_camera_stream_mjpeg(httpd_req_t *req, robot_camera_slow_mode_fn_t slow_mode_fn)
{
    esp_err_t ret = robot_camera_init();
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, esp_err_to_name(ret));
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_stream_clients >= CAMERA_MAX_STREAM_CLIENTS) {
        xSemaphoreGive(s_lock);
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "camera stream already in use");
    }
    s_stream_clients++;
    xSemaphoreGive(s_lock);

    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    while (true) {
        const uint8_t *frame = NULL;
        size_t frame_len = 0;
        uint8_t *jpeg_copy = NULL;
        size_t jpeg_copy_len = 0;

        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1200)) != pdTRUE) {
            s_dropped_frames++;
            continue;
        }
        ret = dequeue_frame_locked(&frame, &frame_len);
        if (ret == ESP_OK) {
            ret = copy_latest_locked(&jpeg_copy, &jpeg_copy_len);
        }
        xSemaphoreGive(s_lock);
        if (ret != ESP_OK) {
            free(jpeg_copy);
            break;
        }

        char header[96];
        int header_len = snprintf(header, sizeof(header),
                                  "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                                  (unsigned)jpeg_copy_len);
        if ((httpd_resp_send_chunk(req, header, header_len) != ESP_OK) ||
            (httpd_resp_send_chunk(req, (const char *)jpeg_copy, jpeg_copy_len) != ESP_OK) ||
            (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK)) {
            free(jpeg_copy);
            break;
        }
        free(jpeg_copy);

        bool slow = slow_mode_fn != NULL && slow_mode_fn();
        uint32_t delay_ms = slow ? (1000 / CONFIG_ROBOT_CAMERA_STREAM_SLOW_FPS) : (1000 / CONFIG_ROBOT_CAMERA_STREAM_FPS);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_stream_clients > 0) {
        s_stream_clients--;
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}
