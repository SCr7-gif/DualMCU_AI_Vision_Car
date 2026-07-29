#include "qwen_realtime_client.h"

#include "audio_codec.h"
#include "behavior_manager.h"
#include "board_config.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "robot_tools.h"
#include "secret_manager.h"
#include "sensor_cache.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "qwen_realtime";

#define QWEN_OUTPUT_SAMPLE_RATE_HZ 24000
#define QWEN_OUTPUT_BITS_PER_SAMPLE 16
#define QWEN_OUTPUT_CHANNEL_COUNT 1
#define QWEN_AUDIO_SPOOL_BUFFER_BYTES (512u * 1024u)
#define QWEN_AUDIO_SPOOL_FALLBACK_BYTES (96u * 1024u)
#define QWEN_AUDIO_RING_BUFFER_BYTES (64u * 1024u)
#define QWEN_AUDIO_PREBUFFER_BYTES (24u * 1024u)
#define QWEN_AUDIO_RING_TARGET_BYTES (24u * 1024u)
#define QWEN_AUDIO_PLAY_CHUNK_BYTES 2048u
#define QWEN_MIC_SPEECH_PEAK_THRESHOLD 260
#define QWEN_MIC_SPEECH_NOISE_MARGIN 100
#define QWEN_MIC_BARGE_IN_PEAK_THRESHOLD 9000
#define QWEN_MIC_BARGE_IN_NOISE_MARGIN 5000
#define QWEN_MIC_MIN_VOICED_MS 280
#define QWEN_MIC_BARGE_IN_MIN_VOICED_MS 700
#define QWEN_MIC_END_SILENCE_MS 900
#define QWEN_MIC_FALSE_START_SILENCE_MS 800
#define QWEN_MIC_PREROLL_CHUNKS 8
#define QWEN_MIC_MAX_UTTERANCE_MS 10000
#define QWEN_MIC_UPLOAD_GAIN_NUM 8
#define QWEN_MIC_UPLOAD_GAIN_DEN 1

#if BOARD_AUDIO_SAMPLE_RATE_HZ != QWEN_OUTPUT_SAMPLE_RATE_HZ
#error "BOARD_AUDIO_SAMPLE_RATE_HZ must match Qwen output PCM sample rate"
#endif

static esp_websocket_client_handle_t s_client;
static qwen_realtime_state_t s_state = QWEN_REALTIME_DISCONNECTED;
static bool s_session_ready;
static char s_auth_headers[SECRET_QWEN_API_KEY_MAX + 64];
static char s_ws_uri[SECRET_QWEN_ENDPOINT_MAX + SECRET_QWEN_MODEL_MAX + 16];
static char *s_ws_text_buf;
static int s_ws_text_payload_len;
static TaskHandle_t s_audio_task;
static TaskHandle_t s_audio_play_task;
static RingbufHandle_t s_audio_ring_buffer;
static size_t s_audio_ring_buffer_size;
static uint8_t *s_audio_spool_buffer;
static size_t s_audio_spool_size;
static size_t s_audio_spool_read_pos;
static size_t s_audio_spool_write_pos;
static size_t s_audio_spool_used;
static SemaphoreHandle_t s_audio_spool_mutex;
static volatile bool s_playing_audio;
static volatile bool s_response_active;
static volatile bool s_stop_audio_playback;
static volatile bool s_voice_enabled;
static volatile bool s_audio_stream_done;
static volatile bool s_speaking_mode;
static volatile bool s_audio_receive_blocked;
static volatile bool s_barge_in_drop_audio;
static volatile uint32_t s_audio_underflow_count;
static volatile uint32_t s_spool_overflow_count;
static volatile uint32_t s_ring_overflow_count;
static volatile uint32_t s_dropped_audio_bytes;
static volatile uint32_t s_qwen_audio_delta_count;
static volatile uint32_t s_last_i2s_write_time_ms;
static TickType_t s_mic_resume_tick;
static TickType_t s_last_speaking_led_tick;
static int16_t s_mic_preroll[QWEN_MIC_PREROLL_CHUNKS][480];
static size_t s_mic_preroll_counts[QWEN_MIC_PREROLL_CHUNKS];
static volatile uint32_t s_last_direct_robot_action_ms;

static void drain_audio_ring_buffer(void);
static void audio_spool_reset(void);
static void audio_spool_feed_local_test_tone(uint32_t duration_ms);
static void request_audio_stop(void);

static cJSON *make_string_array_2(const char *a, const char *b)
{
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateString(a));
    cJSON_AddItemToArray(arr, cJSON_CreateString(b));
    return arr;
}

static cJSON *tool_param_string(const char *description)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "string");
    cJSON_AddStringToObject(p, "description", description);
    return p;
}

static cJSON *tool_param_integer(const char *description)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", "integer");
    cJSON_AddStringToObject(p, "description", description);
    return p;
}

static void add_required(cJSON *schema, const char *a, const char *b, const char *c)
{
    cJSON *required = cJSON_CreateArray();
    if (a != NULL) {
        cJSON_AddItemToArray(required, cJSON_CreateString(a));
    }
    if (b != NULL) {
        cJSON_AddItemToArray(required, cJSON_CreateString(b));
    }
    if (c != NULL) {
        cJSON_AddItemToArray(required, cJSON_CreateString(c));
    }
    cJSON_AddItemToObject(schema, "required", required);
}

static cJSON *make_tool(const char *name, const char *description, cJSON *properties, const char *required_a, const char *required_b, const char *required_c)
{
    cJSON *tool = cJSON_CreateObject();
    cJSON *function = cJSON_CreateObject();
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddItemToObject(tool, "function", function);
    cJSON_AddStringToObject(function, "name", name);
    cJSON_AddStringToObject(function, "description", description);
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON_AddItemToObject(schema, "properties", properties);
    add_required(schema, required_a, required_b, required_c);
    cJSON_AddItemToObject(function, "parameters", schema);
    return tool;
}

static void add_tools(cJSON *session)
{
    cJSON *tools = cJSON_CreateArray();

    cJSON_AddItemToArray(tools, make_tool("robot_get_status", "Read the current robot, sensor, gimbal, network, and behavior status.", cJSON_CreateObject(), NULL, NULL, NULL));
    cJSON_AddItemToArray(tools, make_tool("robot_stop", "Immediately stop the car and cancel the current motion.", cJSON_CreateObject(), NULL, NULL, NULL));

    cJSON *move_props = cJSON_CreateObject();
    cJSON_AddItemToObject(move_props, "direction", tool_param_string("forward/backward/left/right/rotate_left/rotate_right"));
    cJSON_AddItemToObject(move_props, "speed", tool_param_integer("0 to 100. ESP32 and STM32 will apply safety limits again."));
    cJSON_AddItemToObject(move_props, "duration_ms", tool_param_integer("Motion duration. Prefer 1200 to 5000 ms for visible car movement."));
    cJSON_AddItemToArray(tools, make_tool("robot_move", "Move the robot through the high-level safety layer.", move_props, "direction", "speed", "duration_ms"));

    cJSON *gimbal_props = cJSON_CreateObject();
    cJSON_AddItemToObject(gimbal_props, "pan", tool_param_integer("Pan angle, 0 to 180."));
    cJSON_AddItemToObject(gimbal_props, "tilt", tool_param_integer("Tilt angle, 30 to 150."));
    cJSON_AddItemToObject(gimbal_props, "duration_ms", tool_param_integer("Smooth movement duration in ms."));
    cJSON_AddItemToArray(tools, make_tool("robot_gimbal_set", "Set the gimbal angle.", gimbal_props, "pan", "tilt", "duration_ms"));

    cJSON *gimbal_action_props = cJSON_CreateObject();
    cJSON_AddItemToObject(gimbal_action_props, "action_name", tool_param_string("center/look_left_soft/look_right_soft/look_up_soft/look_down_soft/scan_slow/scan_fast/nod_yes/shake_no/curious_tilt/sleepy/wake_up/shy_look_down/happy_wiggle/thinking_motion"));
    cJSON_AddItemToArray(tools, make_tool("robot_gimbal_action", "Run a named natural gimbal action.", gimbal_action_props, "action_name", NULL, NULL));

    cJSON *led_props = cJSON_CreateObject();
    cJSON_AddItemToObject(led_props, "effect", tool_param_string("idle/listening/speaking/thinking/error/cute/patrol/solid"));
    cJSON_AddItemToObject(led_props, "r", tool_param_integer("Red, 0 to 255. Use 0 for the default color."));
    cJSON_AddItemToObject(led_props, "g", tool_param_integer("Green, 0 to 255. Use 0 for the default color."));
    cJSON_AddItemToObject(led_props, "b", tool_param_integer("Blue, 0 to 255. Use 0 for the default color."));
    cJSON_AddItemToObject(led_props, "duration_ms", tool_param_integer("Duration in ms. Use 0 to keep the effect."));
    cJSON_AddItemToArray(tools, make_tool("robot_led_effect", "Set the robot status LED effect.", led_props, "effect", NULL, NULL));

    cJSON *behavior_props = cJSON_CreateObject();
    cJSON_AddItemToObject(behavior_props, "name", tool_param_string("dance/patrol/stop_patrol"));
    cJSON_AddItemToArray(tools, make_tool("robot_behavior", "Run a high-level robot behavior.", behavior_props, "name", NULL, NULL));

    cJSON *patrol_decision_props = cJSON_CreateObject();
    cJSON_AddItemToObject(patrol_decision_props, "decision", tool_param_string("left/right only. Use after patrol obstacle photos."));
    cJSON_AddItemToArray(tools, make_tool("robot_patrol_decision", "Tell ESP32 whether patrol should rotate left or right after the left/front/right obstacle photos.", patrol_decision_props, "decision", NULL, NULL));

    cJSON_AddItemToObject(session, "tools", tools);
}

static const char *build_realtime_uri(const secret_config_t *secret)
{
    if (strstr(secret->qwen_endpoint, "model=") != NULL) {
        snprintf(s_ws_uri, sizeof(s_ws_uri), "%s", secret->qwen_endpoint);
    } else {
        const char *sep = (strchr(secret->qwen_endpoint, '?') == NULL) ? "?" : "&";
        snprintf(s_ws_uri,
                 sizeof(s_ws_uri),
                 "%s%smodel=%s",
                 secret->qwen_endpoint,
                 sep,
                 secret->qwen_model);
    }
    return s_ws_uri;
}

static esp_err_t ws_send_json(cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    if (text == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int len = (int)strlen(text);
    int ret = esp_websocket_client_send_text(s_client, text, len, pdMS_TO_TICKS(2000));
    free(text);
    return (ret == len) ? ESP_OK : ESP_FAIL;
}

static esp_err_t send_response_create(bool allow_active)
{
    if (s_response_active && !allow_active) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *response = cJSON_CreateObject();
    if (response == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(response, "type", "response.create");
    esp_err_t ret = ws_send_json(response);
    cJSON_Delete(response);
    if (ret == ESP_OK) {
        if (!s_speaking_mode) {
            drain_audio_ring_buffer();
            audio_spool_reset();
        }
        s_response_active = true;
        s_audio_stream_done = false;
        s_audio_receive_blocked = false;
    }
    return ret;
}

static esp_err_t send_response_cancel(void)
{
    if ((s_client == NULL) || !s_response_active) {
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "response.cancel");
    esp_err_t ret = ws_send_json(root);
    cJSON_Delete(root);
    if (ret == ESP_OK) {
        s_response_active = false;
        s_audio_stream_done = true;
    }
    return ret;
}

static esp_err_t send_audio_commit(void)
{
    if ((s_client == NULL) || !qwen_realtime_client_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "input_audio_buffer.commit");
    esp_err_t ret = ws_send_json(root);
    cJSON_Delete(root);
    return ret;
}

static esp_err_t send_session_update(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *session = cJSON_CreateObject();
    secret_config_t secret = secret_manager_get_config();

    if ((root == NULL) || (session == NULL)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "session.update");
    cJSON_AddItemToObject(root, "session", session);
    cJSON_AddStringToObject(session, "model", secret.qwen_model);
    cJSON_AddItemToObject(session, "modalities", make_string_array_2("text", "audio"));
    cJSON_AddStringToObject(session, "input_audio_format", "pcm");
    cJSON_AddStringToObject(session, "output_audio_format", "pcm");
    cJSON_AddStringToObject(session,
                            "instructions",
                            "You are the Chinese voice brain of a dual-MCU AI vision car. "
                            "The robot car is named eagle fly. "
                            "Reply in Simplified Chinese. Use only the declared high-level robot tools. "
                            "Never ask for direct PWM, GPIO, or motor driver access. "
                            "For simple greetings, acknowledgements, or chat replies, do not call tools. "
                            "Keep robot voice replies short and natural. By default answer in no more than two short Chinese sentences. "
                            "Do not give long explanations unless the user explicitly asks for details. "
                            "For action replies, prefer one short acknowledgement after the matching tool call. "
                            "If the user asks for a physical action, you MUST call the matching tool and must not only say you will do it. "
                            "Motor commands are mandatory tool calls: 前进/向前走/往前走 => robot_move forward; 后退/向后退 => robot_move backward; "
                            "左转/向左转 => robot_move left; 右转/向右转 => robot_move right; "
                            "原地左转/左旋 => robot_move rotate_left; 原地右转/右旋 => robot_move rotate_right; 停止/停下/急停 => robot_stop. "
                            "Use direction=forward/backward/left/right/rotate_left/rotate_right, speed about 30, duration_ms about 1800 unless the user says otherwise. "
                            "Gimbal commands are mandatory tool calls: 云台回中/回中/居中 => robot_gimbal_action center; "
                            "看左/云台向左 => robot_gimbal_action look_left_soft; 看右/云台向右 => robot_gimbal_action look_right_soft; "
                            "抬头/看上/云台向上 => robot_gimbal_action look_up_soft; 低头/看下/云台向下 => robot_gimbal_action look_down_soft. "
                            "Only two high-level behaviors are available: robot_behavior dance and robot_behavior patrol. "
                            "Never answer action requests with only text like '好的' unless you already called the tool. "
                            "Mandatory UTF-8 command map: "
                            "\xE5\x89\x8D\xE8\xBF\x9B/\xE5\x90\x91\xE5\x89\x8D => robot_move forward; "
                            "\xE5\x90\x8E\xE9\x80\x80/\xE5\x90\x91\xE5\x90\x8E => robot_move backward; "
                            "\xE5\xB7\xA6\xE8\xBD\xAC => robot_move left; "
                            "\xE5\x8F\xB3\xE8\xBD\xAC => robot_move right; "
                            "\xE5\x81\x9C\xE6\xAD\xA2/\xE6\x80\xA5\xE5\x81\x9C => robot_stop; "
                            "\xE4\xBA\x91\xE5\x8F\xB0\xE5\x9B\x9E\xE4\xB8\xAD => robot_gimbal_action center. "
                            "High-level behaviors are mandatory tool calls: "
                            "\xE8\xB7\xB3\xE8\x88\x9E/\xE8\xA1\xA8\xE6\xBC\x94 => robot_behavior dance; "
                            "\xE5\xBC\x80\xE5\xA7\x8B\xE5\xB7\xA1\xE9\x80\xBB/\xE5\xB7\xA1\xE9\x80\xBB => robot_behavior patrol. "
                            "Patrol is continuous until the user explicitly says stop patrol and you call robot_behavior stop_patrol. "
                            "After patrol obstacle photos, you MUST call robot_patrol_decision with decision=left or decision=right only. "
                            "In the same Chinese reply, say what is on the left, front, and right, and never say image 1/2/3. "
                            "Before moving forward, consider distance and safety. If there is risk, call robot_stop first and explain briefly. "
                            "When a user asks about status, call robot_get_status before answering.");
    /* ESP32 does local VAD and sends input_audio_buffer.commit itself.
     * Keep Qwen server VAD disabled so it cannot auto-commit the same audio.
     */
    cJSON_AddNullToObject(session, "turn_detection");
    add_tools(session);

    esp_err_t ret = ws_send_json(root);
    cJSON_Delete(root);
    return ret;
}

static size_t audio_ring_free_bytes(void)
{
    if (s_audio_ring_buffer == NULL) {
        return 0;
    }
    return xRingbufferGetCurFreeSize(s_audio_ring_buffer);
}

static size_t audio_ring_used_bytes(void)
{
    if ((s_audio_ring_buffer == NULL) || (s_audio_ring_buffer_size == 0)) {
        return 0;
    }
    size_t free_bytes = audio_ring_free_bytes();
    return (free_bytes <= s_audio_ring_buffer_size) ? (s_audio_ring_buffer_size - free_bytes) : 0;
}

static bool audio_spool_init(void)
{
    if (s_audio_spool_buffer != NULL) {
        return true;
    }

    if (s_audio_spool_mutex == NULL) {
        s_audio_spool_mutex = xSemaphoreCreateMutex();
        if (s_audio_spool_mutex == NULL) {
            ESP_LOGE(TAG, "audio spool mutex create failed");
            return false;
        }
    }

    s_audio_spool_size = QWEN_AUDIO_SPOOL_BUFFER_BYTES;
    s_audio_spool_buffer = (uint8_t *)heap_caps_malloc(s_audio_spool_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_audio_spool_buffer == NULL) {
        /*
         * 当前 sdkconfig 如果没打开 CONFIG_SPIRAM，PSRAM 分配一定失败。
         * 为了先恢复喇叭出声，降级到内部 RAM 小 spool；这样不会有 10 秒缓存，
         * 但 Qwen 短回复和本地闭环测试可以立刻恢复。
         */
        s_audio_spool_size = QWEN_AUDIO_SPOOL_FALLBACK_BYTES;
        s_audio_spool_buffer = (uint8_t *)heap_caps_malloc(s_audio_spool_size, MALLOC_CAP_8BIT);
        if (s_audio_spool_buffer == NULL) {
            s_audio_spool_size = 0;
            ESP_LOGE(TAG,
                     "audio_spool_buffer alloc failed: psram=%u fallback=%u",
                     (unsigned)QWEN_AUDIO_SPOOL_BUFFER_BYTES,
                     (unsigned)QWEN_AUDIO_SPOOL_FALLBACK_BYTES);
            return false;
        }
        ESP_LOGW(TAG,
                 "PSRAM spool unavailable, fallback to internal audio_spool_buffer=%u bytes. Enable CONFIG_SPIRAM for 512KB.",
                 (unsigned)s_audio_spool_size);
        return true;
    }

    ESP_LOGI(TAG,
             "audio_spool_buffer ready: %u bytes PSRAM, format=PCM/%dHz/%dbit/mono",
             (unsigned)s_audio_spool_size,
             QWEN_OUTPUT_SAMPLE_RATE_HZ,
             QWEN_OUTPUT_BITS_PER_SAMPLE);
    return true;
}

static size_t audio_spool_used_bytes(void)
{
    size_t used = 0;
    if ((s_audio_spool_mutex != NULL) && (xSemaphoreTake(s_audio_spool_mutex, pdMS_TO_TICKS(20)) == pdTRUE)) {
        used = s_audio_spool_used;
        xSemaphoreGive(s_audio_spool_mutex);
    }
    return used;
}

static size_t audio_spool_free_bytes(void)
{
    size_t free_bytes = 0;
    if ((s_audio_spool_mutex != NULL) && (xSemaphoreTake(s_audio_spool_mutex, pdMS_TO_TICKS(20)) == pdTRUE)) {
        free_bytes = (s_audio_spool_size >= s_audio_spool_used) ? (s_audio_spool_size - s_audio_spool_used) : 0;
        xSemaphoreGive(s_audio_spool_mutex);
    }
    return free_bytes;
}

static void audio_spool_reset(void)
{
    if ((s_audio_spool_mutex == NULL) || (xSemaphoreTake(s_audio_spool_mutex, pdMS_TO_TICKS(50)) != pdTRUE)) {
        return;
    }

    s_audio_spool_read_pos = 0;
    s_audio_spool_write_pos = 0;
    s_audio_spool_used = 0;
    xSemaphoreGive(s_audio_spool_mutex);
}

static bool audio_spool_write(const uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0) || (s_audio_spool_buffer == NULL) || (s_audio_spool_mutex == NULL)) {
        return false;
    }

    if (xSemaphoreTake(s_audio_spool_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }

    size_t free_bytes = (s_audio_spool_size >= s_audio_spool_used) ? (s_audio_spool_size - s_audio_spool_used) : 0;
    if (len > free_bytes) {
        xSemaphoreGive(s_audio_spool_mutex);
        return false;
    }

    size_t first = s_audio_spool_size - s_audio_spool_write_pos;
    if (first > len) {
        first = len;
    }
    memcpy(s_audio_spool_buffer + s_audio_spool_write_pos, data, first);
    if (len > first) {
        memcpy(s_audio_spool_buffer, data + first, len - first);
    }

    s_audio_spool_write_pos = (s_audio_spool_write_pos + len) % s_audio_spool_size;
    s_audio_spool_used += len;
    xSemaphoreGive(s_audio_spool_mutex);
    return true;
}

static size_t audio_spool_peek(uint8_t *out, size_t max_len)
{
    if ((out == NULL) || (max_len == 0) || (s_audio_spool_buffer == NULL) || (s_audio_spool_mutex == NULL)) {
        return 0;
    }

    if (xSemaphoreTake(s_audio_spool_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return 0;
    }

    size_t len = (s_audio_spool_used < max_len) ? s_audio_spool_used : max_len;
    size_t first = s_audio_spool_size - s_audio_spool_read_pos;
    if (first > len) {
        first = len;
    }
    memcpy(out, s_audio_spool_buffer + s_audio_spool_read_pos, first);
    if (len > first) {
        memcpy(out + first, s_audio_spool_buffer, len - first);
    }

    xSemaphoreGive(s_audio_spool_mutex);
    return len;
}

static void audio_spool_consume(size_t len)
{
    if ((len == 0) || (s_audio_spool_mutex == NULL) || (xSemaphoreTake(s_audio_spool_mutex, pdMS_TO_TICKS(20)) != pdTRUE)) {
        return;
    }

    if (len > s_audio_spool_used) {
        len = s_audio_spool_used;
    }
    s_audio_spool_read_pos = (s_audio_spool_read_pos + len) % s_audio_spool_size;
    s_audio_spool_used -= len;
    xSemaphoreGive(s_audio_spool_mutex);
}

static void drain_audio_ring_buffer(void)
{
    if (s_audio_ring_buffer == NULL) {
        return;
    }

    size_t item_size = 0;
    uint8_t *item = NULL;
    while ((item = (uint8_t *)xRingbufferReceiveUpTo(s_audio_ring_buffer, &item_size, 0, QWEN_AUDIO_PLAY_CHUNK_BYTES)) != NULL) {
        vRingbufferReturnItem(s_audio_ring_buffer, item);
    }
}

static void refill_play_ring_from_spool(void)
{
    uint8_t temp[QWEN_AUDIO_PLAY_CHUNK_BYTES];

    while ((audio_ring_used_bytes() < QWEN_AUDIO_RING_TARGET_BYTES) && (audio_spool_used_bytes() > 0)) {
        size_t free_bytes = audio_ring_free_bytes();
        if (free_bytes < 512u) {
            break;
        }

        size_t max_len = QWEN_AUDIO_PLAY_CHUNK_BYTES;
        /* FreeRTOS ringbuffer 每个 item 有内部头部，留一点余量避免误判可写。 */
        size_t writable_bytes = (free_bytes > 32u) ? (free_bytes - 32u) : 0;
        if (max_len > writable_bytes) {
            max_len = writable_bytes;
        }
        if ((max_len & 1u) != 0) {
            max_len--;
        }
        if (max_len == 0) {
            break;
        }

        size_t len = audio_spool_peek(temp, max_len);
        if ((len & 1u) != 0) {
            len--;
        }
        if (len == 0) {
            break;
        }

        /*
         * play ring 只是 I2S 抗抖动小缓冲。写入失败时不消费 spool，
         * 这样音频仍保存在大缓存里，不会悄悄丢后半段。
         */
        if (xRingbufferSend(s_audio_ring_buffer, temp, len, 0) != pdTRUE) {
            s_ring_overflow_count++;
            ESP_LOGW(TAG,
                     "audio_play_ring full: ring_used=%u ring_free=%u spool_used=%u ring_overflow_count=%u",
                     (unsigned)audio_ring_used_bytes(),
                     (unsigned)audio_ring_free_bytes(),
                     (unsigned)audio_spool_used_bytes(),
                     (unsigned)s_ring_overflow_count);
            break;
        }

        audio_spool_consume(len);
    }
}

static void audio_spool_feed_local_test_tone(uint32_t duration_ms)
{
    if (duration_ms == 0) {
        duration_ms = 1000;
    }

    uint32_t frames_left = (QWEN_OUTPUT_SAMPLE_RATE_HZ * duration_ms) / 1000u;
    uint32_t phase = 0;
    const uint32_t period = QWEN_OUTPUT_SAMPLE_RATE_HZ / 1000u;
    int16_t pcm[480]; /* 20ms mono PCM */

    while (frames_left > 0) {
        size_t frames = frames_left;
        if (frames > (sizeof(pcm) / sizeof(pcm[0]))) {
            frames = sizeof(pcm) / sizeof(pcm[0]);
        }

        for (size_t i = 0; i < frames; i++) {
            pcm[i] = ((phase / (period / 2u)) & 1u) ? 9000 : -9000;
            phase = (phase + 1u) % period;
        }

        if (!audio_spool_write((const uint8_t *)pcm, frames * sizeof(int16_t))) {
            s_spool_overflow_count++;
            s_dropped_audio_bytes += (uint32_t)(frames * sizeof(int16_t));
            ESP_LOGE(TAG,
                     "local qwen audio test spool overflow: frames=%u spool_used=%u spool_free=%u",
                     (unsigned)frames,
                     (unsigned)audio_spool_used_bytes(),
                     (unsigned)audio_spool_free_bytes());
            break;
        }

        frames_left -= frames;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static esp_err_t play_qwen_mono_pcm_to_es8311(const uint8_t *pcm, size_t bytes, uint32_t timeout_ms)
{
    if ((pcm == NULL) || (bytes == 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((bytes & 1u) != 0) {
        bytes--;
    }

    const int16_t *mono = (const int16_t *)pcm;
    size_t mono_samples_left = bytes / sizeof(int16_t);
    int16_t stereo[512];

    while (mono_samples_left > 0) {
        size_t frames = mono_samples_left;
        if (frames > (sizeof(stereo) / sizeof(stereo[0]) / BOARD_AUDIO_CHANNEL_COUNT)) {
            frames = sizeof(stereo) / sizeof(stereo[0]) / BOARD_AUDIO_CHANNEL_COUNT;
        }

        /*
         * Qwen 给的是 mono PCM；ES8311/I2S 用 stereo slot 播放更稳。
         * 这里只在播放边界复制 L/R，不改变 spool 里的原始 mono 数据。
         */
        for (size_t i = 0; i < frames; i++) {
            int16_t v = mono[i];
            stereo[i * BOARD_AUDIO_CHANNEL_COUNT + 0u] = v;
            stereo[i * BOARD_AUDIO_CHANNEL_COUNT + 1u] = v;
        }

        esp_err_t ret = audio_codec_play_pcm(stereo, frames * BOARD_AUDIO_CHANNEL_COUNT, timeout_ms);
        if (ret != ESP_OK) {
            return ret;
        }
        mono += frames;
        mono_samples_left -= frames;
    }

    return ESP_OK;
}

static void request_audio_stop(void)
{
    s_stop_audio_playback = true;
    s_audio_stream_done = true;
    s_mic_resume_tick = 0;
    drain_audio_ring_buffer();
    audio_spool_reset();
    if (!s_playing_audio) {
        audio_codec_play_silence(120);
        audio_codec_set_speaker_enabled(false);
        s_stop_audio_playback = false;
        s_speaking_mode = false;
    }
}

static void begin_user_barge_in(void)
{
    ESP_LOGI(TAG, "voice barge-in: cancel current response and stop speaker");
    s_barge_in_drop_audio = true;
    s_audio_receive_blocked = true;
    send_response_cancel();
    request_audio_stop();
    s_response_active = false;
    s_audio_stream_done = true;
}

static void audio_play_task(void *arg)
{
    (void)arg;

    while (1) {
        if (s_stop_audio_playback) {
            drain_audio_ring_buffer();
            audio_spool_reset();
            audio_codec_play_silence(120);
            audio_codec_set_speaker_enabled(false);
            s_playing_audio = false;
            s_speaking_mode = false;
            s_stop_audio_playback = false;
            s_mic_resume_tick = 0;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        refill_play_ring_from_spool();

        /*
         * 启播前先累计 16KB~32KB。24 kHz / 16-bit / mono 约 48KB/s，
         * 24KB 大约 500ms，能接住 Qwen 快速下发开头时的网络抖动。
         */
        size_t used = audio_ring_used_bytes() + audio_spool_used_bytes();
        if (!s_playing_audio) {
            if ((used < QWEN_AUDIO_PREBUFFER_BYTES) && !(s_audio_stream_done && (used > 0))) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            s_playing_audio = true;
            s_speaking_mode = true;
            audio_codec_set_speaker_enabled(true);
        }

        size_t item_size = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(s_audio_ring_buffer,
                                                          &item_size,
                                                          pdMS_TO_TICKS(80),
                                                          QWEN_AUDIO_PLAY_CHUNK_BYTES);
        if (item == NULL) {
            refill_play_ring_from_spool();
            if (s_audio_stream_done && (audio_spool_used_bytes() == 0) && (audio_ring_used_bytes() == 0)) {
                audio_codec_play_silence(120);
                audio_codec_set_speaker_enabled(false);
                s_playing_audio = false;
                s_speaking_mode = false;
                s_stop_audio_playback = false;
                s_mic_resume_tick = 0;
            } else {
                s_audio_underflow_count++;
            }
            continue;
        }

        if ((item_size & 1u) != 0) {
            item_size--;
        }

        int64_t t0 = esp_timer_get_time();
        esp_err_t ret = play_qwen_mono_pcm_to_es8311(item, item_size, 1000);
        int64_t dt_us = esp_timer_get_time() - t0;
        s_last_i2s_write_time_ms = (uint32_t)(dt_us / 1000);
        vRingbufferReturnItem(s_audio_ring_buffer, item);

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "i2s playback write failed: %s", esp_err_to_name(ret));
            s_audio_underflow_count++;
            audio_codec_play_silence(120);
            audio_codec_set_speaker_enabled(false);
            s_playing_audio = false;
            s_speaking_mode = false;
            s_mic_resume_tick = 0;
        }

        refill_play_ring_from_spool();
        if (s_audio_stream_done && (audio_spool_used_bytes() == 0) && (audio_ring_used_bytes() == 0)) {
            audio_codec_play_silence(120);
            audio_codec_set_speaker_enabled(false);
            s_playing_audio = false;
            s_speaking_mode = false;
            s_mic_resume_tick = 0;
        }
    }
}

static void ensure_audio_play_task(void)
{
    if (!audio_spool_init()) {
        return;
    }

    if (s_audio_ring_buffer == NULL) {
        s_audio_ring_buffer_size = QWEN_AUDIO_RING_BUFFER_BYTES;
        s_audio_ring_buffer = xRingbufferCreate(s_audio_ring_buffer_size, RINGBUF_TYPE_BYTEBUF);
        if (s_audio_ring_buffer == NULL) {
            s_audio_ring_buffer_size = 0;
            ESP_LOGE(TAG, "audio_play_ring create failed");
        } else {
            ESP_LOGI(TAG,
                     "audio_play_ring ready: %u bytes, prebuffer=%u bytes",
                     (unsigned)s_audio_ring_buffer_size,
                     (unsigned)QWEN_AUDIO_PREBUFFER_BYTES);
        }
    }
    if ((s_audio_ring_buffer != NULL) && (s_audio_play_task == NULL)) {
        xTaskCreate(audio_play_task, "qwen_audio_play", 6144, NULL, 6, &s_audio_play_task);
    }
}

static const char *json_string(cJSON *obj, const char *key, const char *fallback)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(item) ? item->valuestring : fallback;
}

static int json_int(cJSON *obj, const char *key, int fallback)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static bool text_contains(const char *text, const char *needle)
{
    return (text != NULL) && (needle != NULL) && (strstr(text, needle) != NULL);
}

static bool text_contains_any3(const char *text, const char *a, const char *b, const char *c)
{
    return text_contains(text, a) || text_contains(text, b) || text_contains(text, c);
}

static bool text_contains_any4(const char *text, const char *a, const char *b, const char *c, const char *d)
{
    return text_contains(text, a) || text_contains(text, b) || text_contains(text, c) || text_contains(text, d);
}

static esp_err_t __attribute__((unused)) execute_direct_robot_text_command(const char *text, char *summary, size_t summary_size)
{
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    const uint8_t speed = 30;
    const uint16_t move_ms = 1800;

    if ((text == NULL) || (text[0] == '\0')) {
        return ESP_ERR_NOT_FOUND;
    }

    if (text_contains_any4(text, "急停", "停止", "停下", "stop")) {
        ret = robot_tools_stop();
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_stop ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "云台回中", "回中", "居中", "center")) {
        ret = robot_tools_gimbal_action("center");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_gimbal_action center ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any3(text, "云台向左", "看左", "向左看")) {
        ret = robot_tools_gimbal_action("look_left_soft");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_gimbal_action look_left_soft ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any3(text, "云台向右", "看右", "向右看")) {
        ret = robot_tools_gimbal_action("look_right_soft");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_gimbal_action look_right_soft ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any3(text, "云台向上", "看上", "抬头")) {
        ret = robot_tools_gimbal_action("look_up_soft");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_gimbal_action look_up_soft ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any3(text, "云台向下", "看下", "低头")) {
        ret = robot_tools_gimbal_action("look_down_soft");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_gimbal_action look_down_soft ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "原地左转", "左旋", "向左旋转", "rotate left")) {
        ret = robot_tools_move("rotate_left", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move rotate_left ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "原地右转", "右旋", "向右旋转", "rotate right")) {
        ret = robot_tools_move("rotate_right", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move rotate_right ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "前进", "向前", "往前", "forward")) {
        ret = robot_tools_move("forward", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move forward ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "后退", "向后", "往后", "backward")) {
        ret = robot_tools_move("backward", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move backward ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "左转", "向左转", "turn left", "left")) {
        ret = robot_tools_move("left", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move left ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "右转", "向右转", "turn right", "right")) {
        ret = robot_tools_move("right", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move right ret=%s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_ERR_NOT_FOUND;
}

static esp_err_t execute_direct_robot_text_command_utf8(const char *text, char *summary, size_t summary_size)
{
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    const uint8_t speed = 30;
    const uint16_t move_ms = 1800;

    if ((text == NULL) || (text[0] == '\0')) {
        return ESP_ERR_NOT_FOUND;
    }

    if (text_contains_any4(text, "\xE5\x81\x9C\xE6\xAD\xA2\xE5\xB7\xA1\xE9\x80\xBB", "\xE7\xBB\x93\xE6\x9D\x9F\xE5\xB7\xA1\xE9\x80\xBB", "stop patrol", "stop_patrol") ||
        text_contains_any3(text, "\xE9\x80\x80\xE5\x87\xBA\xE5\xB7\xA1\xE9\x80\xBB", "\xE5\x81\x9C\xE6\x8E\x89\xE5\xB7\xA1\xE9\x80\xBB", "\xE5\x85\xB3\xE9\x97\xAD\xE5\xB7\xA1\xE9\x80\xBB")) {
        ret = robot_tools_behavior("stop_patrol");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_behavior stop_patrol ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "\xE6\x80\xA5\xE5\x81\x9C", "\xE5\x81\x9C\xE6\xAD\xA2", "\xE5\x81\x9C\xE4\xB8\x8B", "stop")) {
        ret = robot_tools_stop();
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_stop ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any3(text, "\xE8\xB7\xB3\xE8\x88\x9E", "\xE8\xA1\xA8\xE6\xBC\x94", "dance")) {
        ret = robot_tools_behavior("dance");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_behavior dance ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "\xE5\xBC\x80\xE5\xA7\x8B\xE5\xB7\xA1\xE9\x80\xBB", "\xE5\xB7\xA1\xE9\x80\xBB", "\xE8\x87\xAA\xE5\x8A\xA8\xE5\xB7\xA1\xE8\x88\xAA", "patrol")) {
        ret = robot_tools_behavior("patrol");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_behavior patrol ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "\xE4\xBA\x91\xE5\x8F\xB0\xE5\x9B\x9E\xE4\xB8\xAD", "\xE5\x9B\x9E\xE4\xB8\xAD", "\xE5\xB1\x85\xE4\xB8\xAD", "center")) {
        ret = robot_tools_gimbal_action("center");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_gimbal_action center ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any3(text, "\xE4\xBA\x91\xE5\x8F\xB0\xE5\x90\x91\xE5\xB7\xA6", "\xE7\x9C\x8B\xE5\xB7\xA6", "\xE5\x90\x91\xE5\xB7\xA6\xE7\x9C\x8B")) {
        ret = robot_tools_gimbal_action("look_left_soft");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_gimbal_action look_left_soft ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any3(text, "\xE4\xBA\x91\xE5\x8F\xB0\xE5\x90\x91\xE5\x8F\xB3", "\xE7\x9C\x8B\xE5\x8F\xB3", "\xE5\x90\x91\xE5\x8F\xB3\xE7\x9C\x8B")) {
        ret = robot_tools_gimbal_action("look_right_soft");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_gimbal_action look_right_soft ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any3(text, "\xE4\xBA\x91\xE5\x8F\xB0\xE5\x90\x91\xE4\xB8\x8A", "\xE7\x9C\x8B\xE4\xB8\x8A", "\xE6\x8A\xAC\xE5\xA4\xB4")) {
        ret = robot_tools_gimbal_action("look_up_soft");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_gimbal_action look_up_soft ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any3(text, "\xE4\xBA\x91\xE5\x8F\xB0\xE5\x90\x91\xE4\xB8\x8B", "\xE7\x9C\x8B\xE4\xB8\x8B", "\xE4\xBD\x8E\xE5\xA4\xB4")) {
        ret = robot_tools_gimbal_action("look_down_soft");
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_gimbal_action look_down_soft ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "\xE5\x8E\x9F\xE5\x9C\xB0\xE5\xB7\xA6\xE8\xBD\xAC", "\xE5\xB7\xA6\xE6\x97\x8B", "\xE5\x90\x91\xE5\xB7\xA6\xE6\x97\x8B\xE8\xBD\xAC", "rotate left")) {
        ret = robot_tools_move("rotate_left", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move rotate_left ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "\xE5\x8E\x9F\xE5\x9C\xB0\xE5\x8F\xB3\xE8\xBD\xAC", "\xE5\x8F\xB3\xE6\x97\x8B", "\xE5\x90\x91\xE5\x8F\xB3\xE6\x97\x8B\xE8\xBD\xAC", "rotate right")) {
        ret = robot_tools_move("rotate_right", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move rotate_right ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "\xE5\x89\x8D\xE8\xBF\x9B", "\xE5\x90\x91\xE5\x89\x8D", "\xE5\xBE\x80\xE5\x89\x8D", "forward")) {
        ret = robot_tools_move("forward", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move forward ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "\xE5\x90\x8E\xE9\x80\x80", "\xE5\x90\x91\xE5\x90\x8E", "\xE5\xBE\x80\xE5\x90\x8E", "backward")) {
        ret = robot_tools_move("backward", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move backward ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "\xE5\xB7\xA6\xE8\xBD\xAC", "\xE5\x90\x91\xE5\xB7\xA6\xE8\xBD\xAC", "turn left", "left")) {
        ret = robot_tools_move("left", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move left ret=%s", esp_err_to_name(ret));
        return ret;
    }

    if (text_contains_any4(text, "\xE5\x8F\xB3\xE8\xBD\xAC", "\xE5\x90\x91\xE5\x8F\xB3\xE8\xBD\xAC", "turn right", "right")) {
        ret = robot_tools_move("right", speed, move_ms);
        s_last_direct_robot_action_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        snprintf(summary, summary_size, "local robot_move right ret=%s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_ERR_NOT_FOUND;
}

static char *status_json(void)
{
    sensor_cache_snapshot_t st = robot_tools_get_status();
    behavior_manager_status_t bs;
    behavior_manager_get_status(&bs);
    char *out = calloc(1, 1280);
    if (out == NULL) {
        return NULL;
    }
    snprintf(out,
             1280,
             "{\"stm32_online\":%s,\"flags\":%u,\"distance_cm_x10\":%u,\"temperature\":%u,\"humidity\":%u,"
             "\"dht_ok\":%s,\"motor_state\":%u,\"motor_speed\":%u,\"pan\":%u,\"tilt\":%u,\"behavior\":%u,"
             "\"high_behavior\":\"%s\",\"high_step\":%u,\"patrol_loops\":%lu,\"vision_failures\":%u,\"gps\":\"%s\"}",
             st.basic.stm32_online ? "true" : "false",
             st.basic.flags,
             st.sensor.distance_cm_x10,
             st.sensor.temperature,
             st.sensor.humidity,
             st.sensor.dht_ok ? "true" : "false",
             st.motor.state,
             st.motor.current_speed,
             st.gimbal.pan,
             st.gimbal.tilt,
             st.behavior.behavior_id,
             bs.mode_name,
             bs.step,
             (unsigned long)bs.loops,
             bs.vision_failures,
             st.gps_nmea);
    return out;
}

static char *execute_tool(const char *name, const char *arguments_text)
{
    cJSON *args = NULL;
    esp_err_t ret = ESP_ERR_INVALID_ARG;
    char *out = calloc(1, 1280);
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    bool recent_direct_action = (s_last_direct_robot_action_ms != 0u) &&
                                ((now_ms - s_last_direct_robot_action_ms) < 1500u);

    if (out == NULL) {
        return NULL;
    }

    if ((arguments_text != NULL) && (arguments_text[0] != '\0')) {
        args = cJSON_Parse(arguments_text);
    }
    if (args == NULL) {
        args = cJSON_CreateObject();
    }

    if (strcmp(name, "robot_get_status") == 0) {
        char *status = status_json();
        if (status != NULL) {
            snprintf(out, 1280, "{\"ok\":true,\"tool\":\"robot_get_status\",\"esp_err\":0,\"status\":%s}", status);
            free(status);
            cJSON_Delete(args);
            return out;
        }
        ret = ESP_ERR_NO_MEM;
    } else if (recent_direct_action &&
               ((strcmp(name, "robot_stop") == 0) ||
                (strcmp(name, "robot_move") == 0) ||
                (strcmp(name, "robot_gimbal_set") == 0) ||
                (strcmp(name, "robot_gimbal_action") == 0) ||
                (strcmp(name, "robot_behavior") == 0))) {
        ESP_LOGI(TAG, "skip duplicate tool after local direct action: %s", name);
        ret = ESP_OK;
    } else if (strcmp(name, "robot_stop") == 0) {
        ret = robot_tools_stop();
    } else if (strcmp(name, "robot_move") == 0) {
        ret = robot_tools_move(json_string(args, "direction", "forward"),
                               (uint8_t)json_int(args, "speed", 30),
                               (uint16_t)json_int(args, "duration_ms", 1800));
    } else if (strcmp(name, "robot_gimbal_set") == 0) {
        ret = robot_tools_gimbal_set((uint8_t)json_int(args, "pan", 90),
                                     (uint8_t)json_int(args, "tilt", 90),
                                     (uint16_t)json_int(args, "duration_ms", 700));
    } else if (strcmp(name, "robot_gimbal_action") == 0) {
        ret = robot_tools_gimbal_action(json_string(args, "action_name", "center"));
    } else if (strcmp(name, "robot_led_effect") == 0) {
        ret = robot_tools_led_effect(json_string(args, "effect", "idle"),
                                     (uint8_t)json_int(args, "r", 0),
                                     (uint8_t)json_int(args, "g", 0),
                                     (uint8_t)json_int(args, "b", 0),
                                     (uint16_t)json_int(args, "duration_ms", 0));
    } else if (strcmp(name, "robot_behavior") == 0) {
        ret = robot_tools_behavior(json_string(args, "name", "patrol"));
    } else if (strcmp(name, "robot_patrol_decision") == 0) {
        ret = behavior_manager_apply_vision_decision(json_string(args, "decision", "right"));
    } else {
        ret = ESP_ERR_NOT_SUPPORTED;
    }

    char *status = status_json();
    snprintf(out,
             1280,
             "{\"ok\":%s,\"tool\":\"%s\",\"esp_err\":%d,\"status\":%s}",
             (ret == ESP_OK) ? "true" : "false",
             name,
             ret,
             status ? status : "{}");
    free(status);
    cJSON_Delete(args);
    return out;
}

static esp_err_t send_tool_result(const char *call_id, const char *output)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *item = cJSON_CreateObject();
    if ((root == NULL) || (item == NULL)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "conversation.item.create");
    cJSON_AddItemToObject(root, "item", item);
    cJSON_AddStringToObject(item, "type", "function_call_output");
    cJSON_AddStringToObject(item, "call_id", call_id);
    cJSON_AddStringToObject(item, "output", output);
    esp_err_t ret = ws_send_json(root);
    cJSON_Delete(root);

    if (ret == ESP_OK) {
        ret = send_response_create(true);
    }

    return ret;
}

static void handle_function_call_done(cJSON *root)
{
    const char *name = json_string(root, "name", "");
    const char *call_id = json_string(root, "call_id", "");
    const char *arguments = json_string(root, "arguments", "{}");

    if ((name[0] == '\0') || (call_id[0] == '\0')) {
        ESP_LOGW(TAG, "function call missing name/call_id");
        return;
    }

    ESP_LOGI(TAG, "function call: %s args=%s", name, arguments);
    char *output = execute_tool(name, arguments);
    if (output == NULL) {
        output = calloc(1, 33);
        if (output != NULL) {
            strcpy(output, "{\"ok\":false,\"error\":\"no_memory\"}");
        }
    }
    if (output != NULL) {
        s_response_active = false;
        send_tool_result(call_id, output);
        free(output);
    }
}

static void handle_audio_delta(cJSON *root)
{
    const char *delta = json_string(root, "delta", "");
    if (delta[0] == '\0') {
        return;
    }

    if (s_barge_in_drop_audio) {
        ESP_LOGW(TAG, "drop late qwen audio after user barge-in");
        return;
    }

    ensure_audio_play_task();

    size_t input_len = strlen(delta);
    size_t output_len = 0;
    if (mbedtls_base64_decode(NULL, 0, &output_len, (const unsigned char *)delta, input_len) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        return;
    }

    uint8_t *pcm = malloc(output_len);
    if (pcm == NULL) {
        s_dropped_audio_bytes += (uint32_t)output_len;
        ESP_LOGE(TAG,
                 "no memory for qwen pcm decode: bytes=%u dropped_audio_bytes=%u",
                 (unsigned)output_len,
                 (unsigned)s_dropped_audio_bytes);
        return;
    }

    if (mbedtls_base64_decode(pcm, output_len, &output_len, (const unsigned char *)delta, input_len) == 0) {
        TickType_t now = xTaskGetTickCount();
        if ((now - s_last_speaking_led_tick) > pdMS_TO_TICKS(700)) {
            robot_tools_led_effect("speaking", 0, 0, 0, 0);
            s_last_speaking_led_tick = now;
        }

        /*
         * Qwen 返回的是 PCM / 24 kHz / 16-bit / mono。
         * WebSocket 任务只负责 base64 解码并写入 PSRAM spool，
         * 绝不在这里 i2s_write，避免网络解析节奏影响播放。
         */
        s_qwen_audio_delta_count++;
        if ((output_len == 0) || ((output_len & 1u) != 0)) {
            ESP_LOGW(TAG, "invalid qwen pcm bytes=%u", (unsigned)output_len);
            free(pcm);
            return;
        }

        if (s_audio_receive_blocked) {
            s_dropped_audio_bytes += (uint32_t)output_len;
            ESP_LOGW(TAG,
                     "drop qwen audio after spool overflow: bytes=%u dropped_audio_bytes=%u",
                     (unsigned)output_len,
                     (unsigned)s_dropped_audio_bytes);
            free(pcm);
            return;
        }

        s_audio_stream_done = false;
        s_speaking_mode = true;
        if (!audio_spool_write(pcm, output_len)) {
            s_spool_overflow_count++;
            s_dropped_audio_bytes += (uint32_t)output_len;
            s_audio_receive_blocked = true;
            s_audio_stream_done = true;
            ESP_LOGE(TAG,
                     "audio_spool_buffer overflow: pcm_bytes=%u spool_used=%u spool_free=%u "
                     "spool_overflow_count=%u dropped_audio_bytes=%u, cancel response",
                     (unsigned)output_len,
                     (unsigned)audio_spool_used_bytes(),
                     (unsigned)audio_spool_free_bytes(),
                     (unsigned)s_spool_overflow_count,
                     (unsigned)s_dropped_audio_bytes);
            send_response_cancel();
        }
        free(pcm);
        return;
    }
    free(pcm);
}

static esp_err_t append_audio_pcm_chunk(const int16_t *mono, size_t mono_count, unsigned char *b64, size_t b64_size)
{
    size_t b64_len = 0;

    if ((mono == NULL) || (mono_count == 0) || (b64 == NULL) || (b64_size == 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mbedtls_base64_encode(b64,
                              b64_size,
                              &b64_len,
                              (const unsigned char *)mono,
                              mono_count * sizeof(int16_t)) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (b64_len >= b64_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    b64[b64_len] = '\0';
    return qwen_realtime_client_append_audio_base64((const char *)b64);
}

static void apply_upload_gain(int16_t *samples, size_t count)
{
    if ((QWEN_MIC_UPLOAD_GAIN_NUM == QWEN_MIC_UPLOAD_GAIN_DEN) || (samples == NULL)) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        int32_t boosted = ((int32_t)samples[i] * QWEN_MIC_UPLOAD_GAIN_NUM) / QWEN_MIC_UPLOAD_GAIN_DEN;
        if (boosted > INT16_MAX) {
            boosted = INT16_MAX;
        } else if (boosted < INT16_MIN) {
            boosted = INT16_MIN;
        }
        samples[i] = (int16_t)boosted;
    }
}

static void audio_upload_task(void *arg)
{
    (void)arg;
    int16_t i2s_samples[960]; /* 20 ms @ 24 kHz, 16-bit, stereo slot */
    int16_t mono[480];        /* Qwen input: 20 ms @ 24 kHz, 16-bit, mono */
    unsigned char b64[1400];
    bool utterance_active = false;
    uint8_t preroll_pos = 0;
    uint8_t preroll_filled = 0;
    uint32_t voiced_ms = 0;
    uint32_t silence_ms = 0;
    uint32_t utterance_ms = 0;
    int16_t utterance_peak = 0;
    uint16_t noise_peak = 120;

    while (1) {
        TickType_t now = xTaskGetTickCount();
        if (!s_voice_enabled || !qwen_realtime_client_is_connected() ||
            ((int32_t)(s_mic_resume_tick - now) > 0)) {
            utterance_active = false;
            preroll_pos = 0;
            preroll_filled = 0;
            memset(s_mic_preroll_counts, 0, sizeof(s_mic_preroll_counts));
            voiced_ms = 0;
            silence_ms = 0;
            utterance_ms = 0;
            utterance_peak = 0;
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        size_t samples_read = 0;
        if (audio_codec_read_pcm(i2s_samples, sizeof(i2s_samples) / sizeof(i2s_samples[0]), &samples_read, 50) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        size_t mono_count = 0;
        int16_t peak = 0;
        for (size_t i = 0; (i + 1u) < samples_read && mono_count < (sizeof(mono) / sizeof(mono[0])); i += BOARD_AUDIO_CHANNEL_COUNT) {
            int16_t sample = i2s_samples[i];
            int32_t abs_sample = sample;
            if (abs_sample < 0) {
                abs_sample = -abs_sample;
            }
            if (abs_sample > peak) {
                peak = (int16_t)abs_sample;
            }
            mono[mono_count++] = sample;
        }

        if (mono_count == 0) {
            continue;
        }

        apply_upload_gain(mono, mono_count);

        uint32_t chunk_ms = (uint32_t)((mono_count * 1000u) / QWEN_OUTPUT_SAMPLE_RATE_HZ);
        if (chunk_ms == 0) {
            chunk_ms = 20;
        }

        bool ai_audio_active = s_response_active || qwen_realtime_client_is_audio_output_active();
        int16_t dynamic_speech_threshold =
            (int16_t)((noise_peak + QWEN_MIC_SPEECH_NOISE_MARGIN) > QWEN_MIC_SPEECH_PEAK_THRESHOLD
                          ? (noise_peak + QWEN_MIC_SPEECH_NOISE_MARGIN)
                          : QWEN_MIC_SPEECH_PEAK_THRESHOLD);
        int16_t dynamic_barge_threshold =
            (int16_t)((noise_peak + QWEN_MIC_BARGE_IN_NOISE_MARGIN) > QWEN_MIC_BARGE_IN_PEAK_THRESHOLD
                          ? (noise_peak + QWEN_MIC_BARGE_IN_NOISE_MARGIN)
                          : QWEN_MIC_BARGE_IN_PEAK_THRESHOLD);
        int16_t speech_threshold = ai_audio_active ? dynamic_barge_threshold : dynamic_speech_threshold;
        uint32_t min_voiced_ms = ai_audio_active ? QWEN_MIC_BARGE_IN_MIN_VOICED_MS : QWEN_MIC_MIN_VOICED_MS;

        if (!utterance_active) {
            if (ai_audio_active) {
                // The board has no acoustic echo cancellation. While AI audio is playing,
                // speaker output can leak into the mic and falsely cancel the response.
                preroll_pos = 0;
                preroll_filled = 0;
                memset(s_mic_preroll_counts, 0, sizeof(s_mic_preroll_counts));
                continue;
            }

            if (!ai_audio_active && (peak < 2000)) {
                noise_peak = (uint16_t)(((uint32_t)noise_peak * 15u + (uint32_t)peak) / 16u);
            }

            memcpy(s_mic_preroll[preroll_pos], mono, mono_count * sizeof(int16_t));
            s_mic_preroll_counts[preroll_pos] = mono_count;
            preroll_pos = (uint8_t)((preroll_pos + 1u) % QWEN_MIC_PREROLL_CHUNKS);
            if (preroll_filled < QWEN_MIC_PREROLL_CHUNKS) {
                preroll_filled++;
            }

            if (peak < speech_threshold) {
                continue;
            }

            utterance_active = true;
            voiced_ms += chunk_ms;
            silence_ms = 0;
            utterance_ms = chunk_ms;
            utterance_peak = peak;
            ESP_LOGI(TAG,
                     "voice start peak=%d threshold=%d noise=%u barge=%d",
                     peak,
                     speech_threshold,
                     (unsigned)noise_peak,
                     ai_audio_active ? 1 : 0);

            if (preroll_filled > 0) {
                uint8_t oldest = (uint8_t)((preroll_pos + QWEN_MIC_PREROLL_CHUNKS - preroll_filled) % QWEN_MIC_PREROLL_CHUNKS);
                for (uint8_t i = 0; i < preroll_filled; i++) {
                    uint8_t idx = (uint8_t)((oldest + i) % QWEN_MIC_PREROLL_CHUNKS);
                    if (s_mic_preroll_counts[idx] > 0) {
                        append_audio_pcm_chunk(s_mic_preroll[idx], s_mic_preroll_counts[idx], b64, sizeof(b64));
                    }
                }
            } else {
                append_audio_pcm_chunk(mono, mono_count, b64, sizeof(b64));
            }
            continue;
        }

        append_audio_pcm_chunk(mono, mono_count, b64, sizeof(b64));
        utterance_ms += chunk_ms;
        if (peak >= speech_threshold) {
            voiced_ms += chunk_ms;
            silence_ms = 0;
            if (peak > utterance_peak) {
                utterance_peak = peak;
            }
        } else {
            silence_ms += chunk_ms;
        }

        if (utterance_active && (voiced_ms < min_voiced_ms) &&
            ((silence_ms >= QWEN_MIC_FALSE_START_SILENCE_MS) || (utterance_ms >= QWEN_MIC_MAX_UTTERANCE_MS))) {
            ESP_LOGI(TAG,
                     "voice false start discarded peak=%d voiced_ms=%lu silence_ms=%lu total_ms=%lu threshold=%d",
                     utterance_peak,
                     (unsigned long)voiced_ms,
                     (unsigned long)silence_ms,
                     (unsigned long)utterance_ms,
                     speech_threshold);
            utterance_active = false;
            preroll_pos = 0;
            preroll_filled = 0;
            memset(s_mic_preroll_counts, 0, sizeof(s_mic_preroll_counts));
            voiced_ms = 0;
            silence_ms = 0;
            utterance_ms = 0;
            utterance_peak = 0;
            continue;
        }

        if (utterance_active && (voiced_ms >= min_voiced_ms) &&
            ((silence_ms >= QWEN_MIC_END_SILENCE_MS) || (utterance_ms >= QWEN_MIC_MAX_UTTERANCE_MS))) {
            if (s_response_active) {
                begin_user_barge_in();
            }
            esp_err_t commit_ret = send_audio_commit();
            esp_err_t response_ret = (commit_ret == ESP_OK) ? send_response_create(false) : commit_ret;
            ESP_LOGI(TAG,
                     "voice utterance committed peak=%d voiced_ms=%lu silence_ms=%lu total_ms=%lu ret=%s/%s",
                     utterance_peak,
                     (unsigned long)voiced_ms,
                     (unsigned long)silence_ms,
                     (unsigned long)utterance_ms,
                     esp_err_to_name(commit_ret),
                     esp_err_to_name(response_ret));
            utterance_active = false;
            preroll_pos = 0;
            preroll_filled = 0;
            memset(s_mic_preroll_counts, 0, sizeof(s_mic_preroll_counts));
            voiced_ms = 0;
            silence_ms = 0;
            utterance_ms = 0;
            utterance_peak = 0;
        }
    }
}

static const char *find_text_field_recursive(cJSON *node)
{
    if (node == NULL) {
        return NULL;
    }

    if (cJSON_IsObject(node)) {
        cJSON *transcript = cJSON_GetObjectItem(node, "transcript");
        if (cJSON_IsString(transcript) && (transcript->valuestring[0] != '\0')) {
            return transcript->valuestring;
        }

        cJSON *text = cJSON_GetObjectItem(node, "text");
        if (cJSON_IsString(text) && (text->valuestring[0] != '\0')) {
            return text->valuestring;
        }
    }

    cJSON *child = NULL;
    cJSON_ArrayForEach(child, node)
    {
        const char *found = find_text_field_recursive(child);
        if (found != NULL) {
            return found;
        }
    }

    return NULL;
}

static void maybe_execute_transcribed_action(const char *type, cJSON *root)
{
    if ((type == NULL) || (root == NULL)) {
        return;
    }

    if ((strstr(type, "transcript") == NULL) && (strstr(type, "transcription") == NULL)) {
        return;
    }

    if ((strstr(type, "input") == NULL) && (strstr(type, "audio") == NULL)) {
        return;
    }

    const char *text = find_text_field_recursive(root);
    if (text == NULL) {
        return;
    }

    char direct_summary[128];
    esp_err_t direct_ret = execute_direct_robot_text_command_utf8(text, direct_summary, sizeof(direct_summary));
    if (direct_ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "voice transcript direct action: text=\"%s\" %s", text, direct_summary);
    }
}

static void handle_text_message(const char *data, int len)
{
    char *copy = calloc(1, (size_t)len + 1u);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, data, len);

    cJSON *root = cJSON_Parse(copy);
    if (root == NULL) {
        ESP_LOGW(TAG, "bad json from qwen");
        free(copy);
        return;
    }

    const char *type = json_string(root, "type", "");
    if (strcmp(type, "session.updated") == 0) {
        s_session_ready = true;
        ESP_LOGI(TAG, "session updated");
    } else if (strcmp(type, "response.created") == 0) {
        s_barge_in_drop_audio = false;
        s_audio_receive_blocked = false;
        s_response_active = true;
        s_audio_stream_done = false;
        ESP_LOGI(TAG, "response created");
    } else if (strcmp(type, "response.done") == 0) {
        s_response_active = false;
        s_audio_stream_done = true;
        s_barge_in_drop_audio = false;
        ESP_LOGI(TAG, "response done");
    } else if ((strcmp(type, "response.audio.done") == 0) || (strcmp(type, "response.output_audio.done") == 0)) {
        s_audio_stream_done = true;
        ESP_LOGI(TAG, "response audio done");
    } else if (strcmp(type, "response.function_call_arguments.done") == 0) {
        handle_function_call_done(root);
    } else if (strcmp(type, "response.audio.delta") == 0) {
        handle_audio_delta(root);
    } else if ((strstr(type, "transcript") != NULL) || (strstr(type, "text") != NULL)) {
        maybe_execute_transcribed_action(type, root);
        ESP_LOGI(TAG, "qwen event: %s", type);
    } else if (strcmp(type, "error") == 0) {
        s_response_active = false;
        ESP_LOGW(TAG, "qwen error: %s", copy);
    }

    cJSON_Delete(root);
    free(copy);
}

static void handle_ws_text_fragment(const esp_websocket_event_data_t *data)
{
    if ((data->data_ptr == NULL) || (data->data_len <= 0)) {
        return;
    }

    if ((data->payload_len <= data->data_len) && (data->payload_offset == 0)) {
        handle_text_message(data->data_ptr, data->data_len);
        return;
    }

    if ((data->payload_len <= 0) || (data->payload_len > 32768)) {
        ESP_LOGW(TAG, "drop websocket text payload len=%d", data->payload_len);
        free(s_ws_text_buf);
        s_ws_text_buf = NULL;
        s_ws_text_payload_len = 0;
        return;
    }

    if ((data->payload_offset == 0) || (s_ws_text_buf == NULL) || (s_ws_text_payload_len != data->payload_len)) {
        free(s_ws_text_buf);
        s_ws_text_buf = calloc(1, (size_t)data->payload_len + 1u);
        s_ws_text_payload_len = data->payload_len;
        if (s_ws_text_buf == NULL) {
            s_ws_text_payload_len = 0;
            return;
        }
    }

    if ((data->payload_offset < 0) || ((data->payload_offset + data->data_len) > s_ws_text_payload_len)) {
        ESP_LOGW(TAG, "drop websocket text fragment offset=%d len=%d payload=%d",
                 data->payload_offset,
                 data->data_len,
                 s_ws_text_payload_len);
        free(s_ws_text_buf);
        s_ws_text_buf = NULL;
        s_ws_text_payload_len = 0;
        return;
    }

    memcpy(s_ws_text_buf + data->payload_offset, data->data_ptr, data->data_len);
    if ((data->payload_offset + data->data_len) >= s_ws_text_payload_len) {
        handle_text_message(s_ws_text_buf, s_ws_text_payload_len);
        free(s_ws_text_buf);
        s_ws_text_buf = NULL;
        s_ws_text_payload_len = 0;
    }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_state = QWEN_REALTIME_CONNECTED;
        s_session_ready = false;
        s_response_active = false;
        s_audio_stream_done = true;
        s_audio_receive_blocked = false;
        s_speaking_mode = false;
        drain_audio_ring_buffer();
        audio_spool_reset();
        ensure_audio_play_task();
        ESP_LOGI(TAG, "connected");
        send_session_update();
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        s_state = QWEN_REALTIME_DISCONNECTED;
        s_session_ready = false;
        s_response_active = false;
        s_voice_enabled = false;
        request_audio_stop();
        ESP_LOGW(TAG, "disconnected");
        break;

    case WEBSOCKET_EVENT_DATA:
        if (((data->op_code == 0x1) || (data->op_code == 0x0)) && (data->data_ptr != NULL) && (data->data_len > 0)) {
            handle_ws_text_fragment(data);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        s_state = QWEN_REALTIME_ERROR;
        s_response_active = false;
        s_voice_enabled = false;
        request_audio_stop();
        ESP_LOGW(TAG,
                 "websocket error type=%d http=%d tls_err=0x%x tls_stack=%d sock_errno=%d",
                 data->error_handle.error_type,
                 data->error_handle.esp_ws_handshake_status_code,
                 data->error_handle.esp_tls_last_esp_err,
                 data->error_handle.esp_tls_stack_err,
                 data->error_handle.esp_transport_sock_errno);
        break;

    default:
        break;
    }
}

esp_err_t qwen_realtime_client_init(void)
{
    if (!secret_manager_has_qwen_key()) {
        ESP_LOGW(TAG, "qwen api key not configured, realtime client disabled");
        return ESP_OK;
    }

    secret_config_t secret = secret_manager_get_config();
    snprintf(s_auth_headers, sizeof(s_auth_headers), "Authorization: Bearer %s\r\n", secret.qwen_api_key);
    const char *uri = build_realtime_uri(&secret);
    ensure_audio_play_task();

    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .headers = s_auth_headers,
        .buffer_size = 4096,
        .task_stack = 12288,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .network_timeout_ms = 10000,
        .reconnect_timeout_ms = 3000,
    };

    s_client = esp_websocket_client_init(&cfg);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_FAIL, TAG, "websocket init failed");
    ESP_RETURN_ON_ERROR(esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL),
                        TAG, "register events failed");
    s_state = QWEN_REALTIME_DISCONNECTED;
    ESP_LOGI(TAG, "client ready uri=%s", uri);
    return ESP_OK;
}

esp_err_t qwen_realtime_client_start(void)
{
    if (s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((s_state == QWEN_REALTIME_CONNECTING) || (s_state == QWEN_REALTIME_CONNECTED)) {
        return ESP_OK;
    }
    s_state = QWEN_REALTIME_CONNECTING;
    esp_err_t ret = esp_websocket_client_start(s_client);
    if (ret != ESP_OK) {
        s_state = QWEN_REALTIME_ERROR;
    }
    if ((ret == ESP_OK) && (s_audio_task == NULL)) {
        xTaskCreate(audio_upload_task, "qwen_audio_up", 12288, NULL, 5, &s_audio_task);
    }
    return ret;
}

esp_err_t qwen_realtime_client_stop(void)
{
    if (s_client == NULL) {
        return ESP_OK;
    }
    if (s_state == QWEN_REALTIME_DISCONNECTED) {
        return ESP_OK;
    }
    esp_err_t ret = esp_websocket_client_stop(s_client);
    if (ret == ESP_OK) {
        s_state = QWEN_REALTIME_DISCONNECTED;
        s_session_ready = false;
        s_voice_enabled = false;
        request_audio_stop();
    }
    return ret;
}

qwen_realtime_state_t qwen_realtime_client_get_state(void)
{
    return s_state;
}

const char *qwen_realtime_client_state_name(void)
{
    switch (s_state) {
    case QWEN_REALTIME_DISCONNECTED:
        return "disconnected";
    case QWEN_REALTIME_CONNECTING:
        return "connecting";
    case QWEN_REALTIME_CONNECTED:
        return s_session_ready ? "ready" : "connected";
    case QWEN_REALTIME_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

bool qwen_realtime_client_is_connected(void)
{
    return (s_state == QWEN_REALTIME_CONNECTED) && s_session_ready;
}

bool qwen_realtime_client_is_voice_enabled(void)
{
    return s_voice_enabled;
}

bool qwen_realtime_client_is_audio_output_active(void)
{
    return s_speaking_mode || s_playing_audio || (audio_ring_used_bytes() > 0) || (audio_spool_used_bytes() > 0);
}

esp_err_t qwen_realtime_client_play_local_audio_test(uint32_t duration_ms)
{
    ensure_audio_play_task();
    if ((s_audio_spool_buffer == NULL) || (s_audio_ring_buffer == NULL)) {
        ESP_LOGE(TAG, "local qwen audio test failed: audio buffers not ready");
        return ESP_ERR_NO_MEM;
    }

    if (!s_speaking_mode) {
        drain_audio_ring_buffer();
        audio_spool_reset();
    }

    s_audio_receive_blocked = false;
    s_audio_stream_done = false;
    s_speaking_mode = true;
    robot_tools_led_effect("speaking", 0, 0, 0, 0);
    ESP_LOGI(TAG, "local qwen audio closed-loop test start: %u ms", (unsigned)duration_ms);
    audio_spool_feed_local_test_tone(duration_ms);
    s_audio_stream_done = true;
    return ESP_OK;
}

esp_err_t qwen_realtime_client_set_voice_enabled(bool enabled)
{
    if (enabled && !qwen_realtime_client_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    s_voice_enabled = enabled;
    if (!enabled) {
        request_audio_stop();
    } else {
        s_mic_resume_tick = 0;
        robot_tools_led_effect("listening", 0, 0, 0, 0);
    }
    return ESP_OK;
}

esp_err_t qwen_realtime_client_send_internal_text(const char *text)
{
    if ((s_client == NULL) || (text == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!qwen_realtime_client_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_response_active || qwen_realtime_client_is_audio_output_active()) {
        begin_user_barge_in();
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *item = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *part = cJSON_CreateObject();
    if ((root == NULL) || (item == NULL) || (content == NULL) || (part == NULL)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "conversation.item.create");
    cJSON_AddItemToObject(root, "item", item);
    cJSON_AddStringToObject(item, "type", "message");
    cJSON_AddStringToObject(item, "role", "user");
    cJSON_AddItemToObject(item, "content", content);
    cJSON_AddStringToObject(part, "type", "input_text");
    cJSON_AddStringToObject(part, "text", text);
    cJSON_AddItemToArray(content, part);

    esp_err_t ret = ws_send_json(root);
    cJSON_Delete(root);
    if (ret == ESP_OK) {
        ret = send_response_create(false);
    }
    return ret;
}

esp_err_t qwen_realtime_client_send_text(const char *text)
{
    char direct_summary[128];
    esp_err_t direct_ret = execute_direct_robot_text_command_utf8(text, direct_summary, sizeof(direct_summary));
    if (direct_ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "%s", direct_summary);
        return direct_ret;
    }

    return qwen_realtime_client_send_internal_text(text);
}

esp_err_t qwen_realtime_client_append_audio_base64(const char *audio_b64)
{
    if ((s_client == NULL) || (audio_b64 == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!qwen_realtime_client_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "input_audio_buffer.append");
    cJSON_AddStringToObject(root, "audio", audio_b64);
    esp_err_t ret = ws_send_json(root);
    cJSON_Delete(root);
    return ret;
}

static esp_err_t append_silent_audio_before_image(void)
{
    int16_t silence[480] = {0}; /* 20 ms @ 24 kHz mono */
    size_t b64_len = 0;
    int ret = mbedtls_base64_encode(NULL, 0, &b64_len, (const unsigned char *)silence, sizeof(silence));
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        return ESP_FAIL;
    }

    unsigned char *b64 = calloc(1, b64_len + 1u);
    if (b64 == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ret = mbedtls_base64_encode(b64, b64_len + 1u, &b64_len, (const unsigned char *)silence, sizeof(silence));
    if (ret != 0) {
        free(b64);
        return ESP_FAIL;
    }
    b64[b64_len] = '\0';

    esp_err_t send_ret = qwen_realtime_client_append_audio_base64((const char *)b64);
    free(b64);
    return send_ret;
}

esp_err_t qwen_realtime_client_append_image_base64(const char *jpeg_b64)
{
    if ((s_client == NULL) || (jpeg_b64 == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!qwen_realtime_client_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t audio_ret = append_silent_audio_before_image();
    if (audio_ret != ESP_OK) {
        return audio_ret;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "input_image_buffer.append");
    cJSON_AddStringToObject(root, "image", jpeg_b64);

    esp_err_t ret = ws_send_json(root);
    cJSON_Delete(root);
    return ret;
}
