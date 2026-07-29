#include "board_diagnostics.h"

#include "board_config.h"
#include "esp_log.h"
#include "image_sampler.h"
#include "network_manager.h"
#include "qwen_realtime_client.h"
#include "robot_camera.h"
#include "sdkconfig.h"
#include "secret_manager.h"
#include <stdio.h>

static const char *TAG = "board_diag";

esp_err_t board_diagnostics_init(void)
{
    ESP_LOGI(TAG, "diagnostics ready");
    return ESP_OK;
}

void board_diagnostics_log_summary(void)
{
    ESP_LOGI(TAG, "target=%s backend=%s native_wifi=%d net_started=%d net_connected=%d",
             CONFIG_IDF_TARGET,
             network_manager_backend_name(),
             network_manager_has_native_wifi(),
             network_manager_is_started(),
             network_manager_is_connected());
    ESP_LOGI(TAG, "secrets wifi=%d qwen=%d qwen_state=%u/%s qwen_ready=%d",
             secret_manager_has_wifi(),
             secret_manager_has_qwen_key(),
             (unsigned)qwen_realtime_client_get_state(),
             qwen_realtime_client_state_name(),
             qwen_realtime_client_is_connected());
    ESP_LOGI(TAG, "stm32_uart num=%d tx_gpio=%d rx_gpio=%d baud=%d",
             BOARD_STM32_UART_NUM,
             BOARD_STM32_UART_TX_GPIO,
             BOARD_STM32_UART_RX_GPIO,
             BOARD_STM32_UART_BAUDRATE);
    ESP_LOGI(TAG, "i2c port=%d sda=%d scl=%d freq=%d es8311=0x%02X ov5647=0x%02X",
             BOARD_I2C_PORT,
             BOARD_I2C_SDA_GPIO,
             BOARD_I2C_SCL_GPIO,
             BOARD_I2C_FREQ_HZ,
             BOARD_ES8311_I2C_ADDR,
             BOARD_OV5647_I2C_ADDR);
    robot_camera_status_t cam;
    robot_camera_get_status(&cam);
    char fourcc[5] = {0};
    ESP_LOGI(TAG, "camera ready=%d dev=%s size=%lux%lu format=%s fps=%lu last_err=%s",
             cam.ready,
             cam.device,
             (unsigned long)cam.width,
             (unsigned long)cam.height,
             robot_camera_fourcc_string(cam.pixfmt, fourcc),
             (unsigned long)cam.fps,
             esp_err_to_name(cam.last_error));
}

int board_diagnostics_write_json(char *out, size_t out_size)
{
    if ((out == NULL) || (out_size == 0)) {
        return 0;
    }

    robot_camera_status_t cam;
    robot_camera_get_status(&cam);
    char fourcc[5] = {0};

    return snprintf(out, out_size,
                    "{"
                    "\"target\":\"%s\","
                    "\"network\":{\"backend\":\"%s\",\"started\":%s,\"connected\":%s,\"ip\":\"%s\",\"native_wifi\":%s},"
                    "\"secrets\":{\"wifi\":%s,\"qwen\":%s},"
                    "\"qwen\":{\"state\":%u,\"state_name\":\"%s\",\"ready\":%s},"
                    "\"camera\":{\"v4l2_enabled\":%s,\"initialized\":%s,\"ready\":%s,"
                    "\"device\":\"%s\",\"width\":%lu,\"height\":%lu,\"format\":\"%s\","
                    "\"fps\":%lu,\"last_frame_ms\":%lu,\"frames\":%lu,\"dropped_frames\":%lu,"
                    "\"stream_clients\":%lu,\"last_error\":\"%s\"},"
                    "\"stm32_uart\":{\"num\":%d,\"tx_gpio\":%d,\"rx_gpio\":%d,\"baud\":%d},"
                    "\"i2c\":{\"port\":%d,\"sda_gpio\":%d,\"scl_gpio\":%d,\"freq\":%d,"
                    "\"es8311_addr\":%u,\"ov5647_addr\":%u},"
                    "\"image_sampler_active\":%s"
                    "}",
                    CONFIG_IDF_TARGET,
                    network_manager_backend_name(),
                    network_manager_is_started() ? "true" : "false",
                    network_manager_is_connected() ? "true" : "false",
                    network_manager_ip_string(),
                    network_manager_has_native_wifi() ? "true" : "false",
                    secret_manager_has_wifi() ? "true" : "false",
                    secret_manager_has_qwen_key() ? "true" : "false",
                    (unsigned)qwen_realtime_client_get_state(),
                    qwen_realtime_client_state_name(),
                    qwen_realtime_client_is_connected() ? "true" : "false",
                    "true",
                    cam.initialized ? "true" : "false",
                    cam.ready ? "true" : "false",
                    cam.device,
                    (unsigned long)cam.width,
                    (unsigned long)cam.height,
                    robot_camera_fourcc_string(cam.pixfmt, fourcc),
                    (unsigned long)cam.fps,
                    (unsigned long)cam.last_frame_ms,
                    (unsigned long)cam.frames,
                    (unsigned long)cam.dropped_frames,
                    (unsigned long)cam.stream_clients,
                    esp_err_to_name(cam.last_error),
                    BOARD_STM32_UART_NUM,
                    BOARD_STM32_UART_TX_GPIO,
                    BOARD_STM32_UART_RX_GPIO,
                    BOARD_STM32_UART_BAUDRATE,
                    BOARD_I2C_PORT,
                    BOARD_I2C_SDA_GPIO,
                    BOARD_I2C_SCL_GPIO,
                    BOARD_I2C_FREQ_HZ,
                    BOARD_ES8311_I2C_ADDR,
                    BOARD_OV5647_I2C_ADDR,
                    image_sampler_is_active() ? "true" : "false");
}

void board_diagnostics_print_console(void)
{
    char json[1024];
    board_diagnostics_write_json(json, sizeof(json));
    printf("%s\n", json);
}
