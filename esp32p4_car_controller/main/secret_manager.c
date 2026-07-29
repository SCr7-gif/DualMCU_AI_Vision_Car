#include "secret_manager.h"

#include "esp_log.h"
#include "nvs.h"
#include <string.h>

#define SECRET_NAMESPACE "robot_secret"

static const char *TAG = "secret_manager";
static secret_config_t s_config;

static void set_default_text(char *dst, size_t dst_size, const char *text)
{
    if (dst_size == 0) {
        return;
    }
    strncpy(dst, text, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void read_nvs_string(nvs_handle_t nvs, const char *key, char *dst, size_t dst_size)
{
    size_t len = dst_size;
    esp_err_t ret = nvs_get_str(nvs, key, dst, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "read key %s failed: %s", key, esp_err_to_name(ret));
    }
}

esp_err_t secret_manager_init(void)
{
    memset(&s_config, 0, sizeof(s_config));
    set_default_text(s_config.qwen_model, sizeof(s_config.qwen_model), "qwen3-omni-flash-realtime-2025-09-15");
    set_default_text(s_config.qwen_endpoint, sizeof(s_config.qwen_endpoint), "wss://dashscope.aliyuncs.com/api-ws/v1/realtime");

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(SECRET_NAMESPACE, NVS_READONLY, &nvs);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS namespace %s not found, Wi-Fi/Qwen secrets are not configured", SECRET_NAMESPACE);
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    read_nvs_string(nvs, "wifi_ssid", s_config.wifi_ssid, sizeof(s_config.wifi_ssid));
    read_nvs_string(nvs, "wifi_pass", s_config.wifi_password, sizeof(s_config.wifi_password));
    read_nvs_string(nvs, "qwen_key", s_config.qwen_api_key, sizeof(s_config.qwen_api_key));
    read_nvs_string(nvs, "qwen_model", s_config.qwen_model, sizeof(s_config.qwen_model));
    read_nvs_string(nvs, "qwen_endpoint", s_config.qwen_endpoint, sizeof(s_config.qwen_endpoint));
    nvs_close(nvs);

    ESP_LOGI(TAG, "wifi_configured=%d qwen_key_configured=%d model=%s",
             secret_manager_has_wifi(), secret_manager_has_qwen_key(), s_config.qwen_model);
    return ESP_OK;
}

secret_config_t secret_manager_get_config(void)
{
    return s_config;
}

bool secret_manager_has_wifi(void)
{
    return (s_config.wifi_ssid[0] != '\0') && (s_config.wifi_password[0] != '\0');
}

bool secret_manager_has_qwen_key(void)
{
    return s_config.qwen_api_key[0] != '\0';
}
