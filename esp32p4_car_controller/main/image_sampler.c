#include "image_sampler.h"

#include "camera_ov5647.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "qwen_realtime_client.h"
#include <stdlib.h>

static const char *TAG = "image_sampler";

static bool s_active;
static bool s_once;
static uint16_t s_fps;
static uint32_t s_last_upload_ms;
static image_sampler_reason_t s_reason;

static esp_err_t upload_jpeg_to_qwen(const uint8_t *jpeg_data, size_t jpeg_len)
{
    size_t b64_len = 0;

    if ((jpeg_data == NULL) || (jpeg_len == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!qwen_realtime_client_is_connected()) {
        ESP_LOGW(TAG, "qwen is not connected, skip image upload");
        return ESP_ERR_INVALID_STATE;
    }

    int ret = mbedtls_base64_encode(NULL, 0, &b64_len, jpeg_data, jpeg_len);
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        ESP_LOGW(TAG, "base64 size query failed: %d", ret);
        return ESP_FAIL;
    }

    unsigned char *b64 = calloc(1, b64_len + 1u);
    if (b64 == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ret = mbedtls_base64_encode(b64, b64_len + 1u, &b64_len, jpeg_data, jpeg_len);
    if (ret != 0) {
        ESP_LOGW(TAG, "base64 encode failed: %d", ret);
        free(b64);
        return ESP_FAIL;
    }

    b64[b64_len] = '\0';
    esp_err_t send_ret = qwen_realtime_client_append_image_base64((const char *)b64);
    free(b64);
    return send_ret;
}

esp_err_t image_sampler_init(void)
{
    s_active = false;
    s_once = false;
    s_fps = 1;
    s_reason = IMAGE_SAMPLER_REASON_NONE;
    s_last_upload_ms = 0;
    return ESP_OK;
}

void image_sampler_request_once(image_sampler_reason_t reason)
{
    s_active = true;
    s_once = true;
    s_reason = reason;
    s_fps = 1;
}

void image_sampler_start_stream(image_sampler_reason_t reason, uint16_t fps)
{
    s_active = true;
    s_once = false;
    s_reason = reason;
    s_fps = (fps == 0 || fps > 3) ? 1 : fps;
}

void image_sampler_stop(void)
{
    s_active = false;
    s_once = false;
    s_reason = IMAGE_SAMPLER_REASON_NONE;
}

void image_sampler_update(uint32_t now_ms)
{
    uint32_t interval_ms;

    if (!s_active) {
        return;
    }

    if (qwen_realtime_client_is_audio_output_active()) {
        ESP_LOGI(TAG, "AI audio is playing, pause image upload for half-duplex");
        return;
    }

    interval_ms = 1000u / s_fps;
    if ((now_ms - s_last_upload_ms) < interval_ms) {
        return;
    }
    s_last_upload_ms = now_ms;

    uint8_t *jpeg_data = NULL;
    size_t jpeg_len = 0;
    esp_err_t ret = camera_ov5647_capture_jpeg(&jpeg_data, &jpeg_len, 500);
    if (ret == ESP_OK) {
        ret = upload_jpeg_to_qwen(jpeg_data, jpeg_len);
        ESP_LOGI(TAG, "image upload reason=%d len=%u ret=%s", s_reason, (unsigned)jpeg_len, esp_err_to_name(ret));
    } else {
        ESP_LOGW(TAG, "image capture failed reason=%d ret=%s", s_reason, esp_err_to_name(ret));
    }
    camera_ov5647_release_jpeg(jpeg_data);

    if (s_once || (ret == ESP_ERR_NOT_SUPPORTED)) {
        image_sampler_stop();
    }
}

bool image_sampler_is_active(void)
{
    return s_active;
}
