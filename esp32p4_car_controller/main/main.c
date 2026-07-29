#include "audio_codec.h"
#include "behavior_manager.h"
#include "board_diagnostics.h"
#include "board_i2c.h"
#include "camera_ov5647.h"
#include "console_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "image_sampler.h"
#include "network_manager.h"
#include "nvs_flash.h"
#include "qwen_realtime_client.h"
#include "robot_camera.h"
#include "robot_tools.h"
#include "safety_guard.h"
#include "secret_manager.h"
#include "sensor_cache.h"
#include "stm32_link.h"
#include "web_debug.h"

static const char *TAG = "app_main";

static void try_start_qwen_when_network_ready(void)
{
    static bool s_auto_voice_logged;

    if (web_debug_is_manual_mode()) {
        s_auto_voice_logged = false;
        return;
    }

    if (!secret_manager_has_qwen_key()) {
        return;
    }

    if (!network_manager_is_connected()) {
        static bool warned_no_network;
        if (!network_manager_has_native_wifi() && !warned_no_network) {
            ESP_LOGW(TAG, "qwen key exists, but network is not connected; waiting for external Wi-Fi/ETH driver");
            warned_no_network = true;
        }
        return;
    }

    qwen_realtime_state_t state = qwen_realtime_client_get_state();
    if ((state == QWEN_REALTIME_CONNECTED) || (state == QWEN_REALTIME_CONNECTING)) {
        if (qwen_realtime_client_is_connected() && !qwen_realtime_client_is_voice_enabled()) {
            esp_err_t voice_ret = qwen_realtime_client_set_voice_enabled(true);
            if (voice_ret == ESP_OK) {
                if (!s_auto_voice_logged) {
                    ESP_LOGI(TAG, "qwen voice auto-started; robot is listening");
                    s_auto_voice_logged = true;
                }
            } else {
                ESP_LOGW(TAG, "qwen voice auto-start failed: %s", esp_err_to_name(voice_ret));
            }
        }
        return;
    }

    s_auto_voice_logged = false;
    esp_err_t qwen_ret = qwen_realtime_client_start();
    if (qwen_ret != ESP_OK) {
        ESP_LOGW(TAG, "qwen realtime start failed: %s", esp_err_to_name(qwen_ret));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-P4 AI vision car controller start");

    esp_err_t nvs_ret = nvs_flash_init();
    if ((nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES) || (nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    ESP_ERROR_CHECK(secret_manager_init());
    ESP_ERROR_CHECK(board_diagnostics_init());
    ESP_ERROR_CHECK(sensor_cache_init());
    ESP_ERROR_CHECK(safety_guard_init());
    ESP_ERROR_CHECK(image_sampler_init());
    ESP_ERROR_CHECK(behavior_manager_init());

    ESP_ERROR_CHECK(board_i2c_init());
    board_i2c_scan();

    /* 摄像头当前先做 I2C 探测，后续 image_sampler 再接 MIPI CSI 取帧。 */
    esp_err_t cam_ret = camera_ov5647_probe();
    if (cam_ret != ESP_OK) {
        ESP_LOGW(TAG, "camera probe not ready yet: %s", esp_err_to_name(cam_ret));
    }

    esp_err_t audio_ret = audio_codec_init();
    if (audio_ret != ESP_OK) {
        ESP_LOGW(TAG, "audio codec init skipped: %s", esp_err_to_name(audio_ret));
    }

    esp_err_t robot_cam_ret = robot_camera_init();
    if (robot_cam_ret != ESP_OK) {
        ESP_LOGW(TAG, "robot camera init skipped: %s", esp_err_to_name(robot_cam_ret));
    }

    ESP_ERROR_CHECK(stm32_link_init());
    ESP_ERROR_CHECK(stm32_link_start());
    ESP_ERROR_CHECK(robot_tools_init());

    esp_err_t net_ret = network_manager_init();
    if (net_ret != ESP_OK) {
        ESP_LOGW(TAG, "network manager init failed: %s", esp_err_to_name(net_ret));
    }
    ESP_ERROR_CHECK(qwen_realtime_client_init());
    try_start_qwen_when_network_ready();
    ESP_ERROR_CHECK(web_debug_start());
    ESP_ERROR_CHECK(console_control_init());
    board_diagnostics_log_summary();

    while (1) {
        sensor_cache_snapshot_t st = robot_tools_get_status();
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        behavior_manager_update(now_ms);
        image_sampler_update(now_ms);
        try_start_qwen_when_network_ready();
        static TickType_t s_last_alive_log_tick;
        TickType_t now = xTaskGetTickCount();
        TickType_t alive_log_interval = qwen_realtime_client_is_audio_output_active() ? pdMS_TO_TICKS(15000) : pdMS_TO_TICKS(5000);
        if ((now - s_last_alive_log_tick) >= alive_log_interval) {
            s_last_alive_log_tick = now;
            ESP_LOGI(TAG, "alive stm32=%d net=%d dist=%u motor=%u/%u gimbal=%u/%u behavior=%u",
                     st.basic.stm32_online,
                     network_manager_is_connected(),
                     st.sensor.distance_cm_x10,
                     st.motor.state,
                     st.motor.current_speed,
                     st.gimbal.pan,
                     st.gimbal.tilt,
                     st.behavior.behavior_id);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
