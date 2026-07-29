#pragma once

/*
 * Qwen realtime client.
 *
 * Owns the WebSocket session to Qwen Realtime, streams microphone PCM,
 * plays returned PCM through ES8311, and forwards function calls to
 * robot_tools. The AI layer may only call high-level safe robot tools.
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    QWEN_REALTIME_DISCONNECTED = 0,
    QWEN_REALTIME_CONNECTING,
    QWEN_REALTIME_CONNECTED,
    QWEN_REALTIME_ERROR,
} qwen_realtime_state_t;

esp_err_t qwen_realtime_client_init(void);
esp_err_t qwen_realtime_client_start(void);
esp_err_t qwen_realtime_client_stop(void);
qwen_realtime_state_t qwen_realtime_client_get_state(void);
const char *qwen_realtime_client_state_name(void);
bool qwen_realtime_client_is_connected(void);
bool qwen_realtime_client_is_voice_enabled(void);
bool qwen_realtime_client_is_audio_output_active(void);
esp_err_t qwen_realtime_client_set_voice_enabled(bool enabled);
esp_err_t qwen_realtime_client_play_local_audio_test(uint32_t duration_ms);
esp_err_t qwen_realtime_client_send_text(const char *text);
esp_err_t qwen_realtime_client_send_internal_text(const char *text);
esp_err_t qwen_realtime_client_append_audio_base64(const char *audio_b64);
esp_err_t qwen_realtime_client_append_image_base64(const char *jpeg_b64);
