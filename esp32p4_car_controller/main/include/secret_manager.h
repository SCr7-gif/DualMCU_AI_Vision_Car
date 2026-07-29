#pragma once

/*
 * Reads local secrets from NVS: Wi-Fi SSID/password, Qwen API key, model,
 * and endpoint. The firmware logs only whether secrets exist; it never prints
 * the full password or API key.
 */

#include "esp_err.h"
#include <stdbool.h>

#define SECRET_WIFI_SSID_MAX 33
#define SECRET_WIFI_PASSWORD_MAX 65
#define SECRET_QWEN_API_KEY_MAX 160
#define SECRET_QWEN_MODEL_MAX 64
#define SECRET_QWEN_ENDPOINT_MAX 128

typedef struct {
    char wifi_ssid[SECRET_WIFI_SSID_MAX];
    char wifi_password[SECRET_WIFI_PASSWORD_MAX];
    char qwen_api_key[SECRET_QWEN_API_KEY_MAX];
    char qwen_model[SECRET_QWEN_MODEL_MAX];
    char qwen_endpoint[SECRET_QWEN_ENDPOINT_MAX];
} secret_config_t;

esp_err_t secret_manager_init(void);
secret_config_t secret_manager_get_config(void);
bool secret_manager_has_wifi(void);
bool secret_manager_has_qwen_key(void);
