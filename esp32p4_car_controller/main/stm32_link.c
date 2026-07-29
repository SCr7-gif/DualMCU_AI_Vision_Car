#include "stm32_link.h"

#include "board_config.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "safety_guard.h"
#include "sensor_cache.h"
#include <string.h>

#define STM32_FRAME_HEAD0 0xAA
#define STM32_FRAME_HEAD1 0x55
#define STM32_MAX_DATA_LEN 64
#define STM32_MAX_FRAME_LEN (STM32_MAX_DATA_LEN + 5)
#define STM32_HEARTBEAT_MS 1000
#define STM32_STATUS_REQUEST_MS 2000
#define STM32_OFFLINE_MS 3500
#define STM32_CONTROL_RETRY_COUNT 3
#define STM32_CONTROL_RETRY_DELAY_MS 20

#define CMD_STOP 0x10
#define CMD_MOVE_FORWARD 0x11
#define CMD_MOVE_BACKWARD 0x12
#define CMD_TURN_LEFT 0x13
#define CMD_TURN_RIGHT 0x14
#define CMD_ROTATE_LEFT 0x15
#define CMD_ROTATE_RIGHT 0x16
#define CMD_SET_SPEED 0x17
#define CMD_GIMBAL_CENTER 0x20
#define CMD_SET_PAN_TILT 0x23
#define CMD_GIMBAL_ACTION 0x24
#define CMD_LED_RGB 0x30
#define CMD_LED_EFFECT 0x31
#define CMD_REQUEST_STATUS 0x40
#define CMD_BEHAVIOR_START 0x50
#define CMD_BEHAVIOR_STOP 0x51
#define CMD_HEARTBEAT 0x7F

#define CMD_STATUS_BASIC 0x80
#define CMD_STATUS_SENSOR 0x81
#define CMD_STATUS_GPS 0x82
#define CMD_STATUS_GIMBAL 0x83
#define CMD_STATUS_MOTOR 0x84
#define CMD_STATUS_BEHAVIOR 0x85
#define CMD_ERROR 0xE0
#define CMD_HEARTBEAT_ACK 0xFF

static const char *TAG = "stm32_link";

static SemaphoreHandle_t s_tx_lock;
static TaskHandle_t s_task;
static uint8_t s_heartbeat_seq;
static stm32_link_diag_t s_diag;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static uint8_t checksum(const uint8_t *data, uint8_t len)
{
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum;
}

static void put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static esp_err_t send_frame(uint8_t cmd, const uint8_t *payload, uint8_t payload_len)
{
    uint8_t frame[STM32_MAX_FRAME_LEN];
    uint8_t len = (uint8_t)(payload_len + 1u);

    if (payload_len > STM32_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    frame[0] = STM32_FRAME_HEAD0;
    frame[1] = STM32_FRAME_HEAD1;
    frame[2] = len;
    frame[3] = cmd;
    if ((payload != NULL) && (payload_len > 0)) {
        memcpy(&frame[4], payload, payload_len);
    }
    frame[4 + payload_len] = checksum(&frame[2], (uint8_t)(payload_len + 2u));

    if (s_tx_lock != NULL) {
        xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    }
    int written = uart_write_bytes(BOARD_STM32_UART_NUM, frame, payload_len + 5u);
    if (written == (int)(payload_len + 5u)) {
        esp_err_t wait_ret = uart_wait_tx_done(BOARD_STM32_UART_NUM, pdMS_TO_TICKS(100));
        if (wait_ret != ESP_OK) {
            ESP_LOGW(TAG, "UART TX wait timeout cmd=0x%02X", cmd);
        }
    }
    if (s_tx_lock != NULL) {
        xSemaphoreGive(s_tx_lock);
    }

    s_diag.last_tx_cmd = cmd;
    s_diag.last_tx_ms = now_ms();
    if (written == (int)(payload_len + 5u)) {
        s_diag.tx_frames++;
    } else {
        s_diag.tx_failures++;
    }

    return (written == (int)(payload_len + 5u)) ? ESP_OK : ESP_FAIL;
}

static esp_err_t send_control_frame(uint8_t cmd, const uint8_t *payload, uint8_t payload_len)
{
    esp_err_t first_ret = ESP_FAIL;

    for (uint8_t attempt = 0; attempt < STM32_CONTROL_RETRY_COUNT; attempt++) {
        esp_err_t ret = send_frame(cmd, payload, payload_len);
        if (attempt == 0) {
            first_ret = ret;
        }
        vTaskDelay(pdMS_TO_TICKS(STM32_CONTROL_RETRY_DELAY_MS));
    }

    (void)send_frame(CMD_REQUEST_STATUS, NULL, 0);
    return first_ret;
}

static void handle_status_frame(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint32_t tick = now_ms();

    /*
     * A valid checksum means the UART wire and both protocol parsers are alive.
     * Mark the link online before interpreting the command-specific payload.
     */
    s_diag.rx_frames++;
    s_diag.last_rx_cmd = cmd;
    s_diag.last_rx_ms = tick;
    s_diag.last_valid_frame_ms = tick;
    s_diag.online = true;
    sensor_cache_set_online(true, tick);

    switch (cmd) {
    case CMD_STATUS_BASIC:
        s_diag.status_basic_frames++;
        if (len >= 3) {
            sensor_cache_update_basic(data[0], read_u16_le(&data[1]), tick);
        }
        break;

    case CMD_STATUS_SENSOR:
        s_diag.status_sensor_frames++;
        if (len >= 5) {
            sensor_cache_update_sensor(read_u16_le(&data[0]), data[2], data[3], data[4] != 0, tick);
        }
        break;

    case CMD_STATUS_GPS:
        s_diag.status_gps_frames++;
        sensor_cache_update_gps(data, len, tick);
        break;

    case CMD_STATUS_GIMBAL:
        s_diag.status_gimbal_frames++;
        if (len >= 4) {
            sensor_cache_update_gimbal(data[0], data[1], data[2] != 0, data[3], tick);
        }
        break;

    case CMD_STATUS_MOTOR:
        s_diag.status_motor_frames++;
        if (len >= 3) {
            sensor_cache_update_motor(data[0], data[1], data[2], tick);
        }
        break;

    case CMD_STATUS_BEHAVIOR:
        s_diag.status_behavior_frames++;
        if (len >= 3) {
            sensor_cache_update_behavior(data[0], data[1] != 0, data[2], tick);
        }
        break;

    case CMD_HEARTBEAT_ACK:
        s_diag.heartbeat_acks++;
        break;

    case CMD_ERROR:
        if (len >= 2) {
            ESP_LOGW(TAG, "STM32 error: code=0x%02X detail=0x%02X", data[0], data[1]);
        }
        break;

    default:
        ESP_LOGW(TAG, "unknown STM32 frame cmd=0x%02X len=%u", cmd, len);
        s_diag.unknown_frames++;
        break;
    }
}

static void parser_input(uint8_t byte)
{
    typedef enum {
        PARSER_WAIT_HEAD0 = 0,
        PARSER_WAIT_HEAD1,
        PARSER_WAIT_LEN,
        PARSER_WAIT_BODY,
        PARSER_WAIT_CHECKSUM,
    } parser_state_t;

    static parser_state_t state = PARSER_WAIT_HEAD0;
    static uint8_t len;
    static uint8_t body[STM32_MAX_DATA_LEN + 1];
    static uint8_t pos;

    switch (state) {
    case PARSER_WAIT_HEAD0:
        if (byte == STM32_FRAME_HEAD0) {
            state = PARSER_WAIT_HEAD1;
        }
        break;

    case PARSER_WAIT_HEAD1:
        state = (byte == STM32_FRAME_HEAD1) ? PARSER_WAIT_LEN : PARSER_WAIT_HEAD0;
        break;

    case PARSER_WAIT_LEN:
        if ((byte == 0) || (byte > (STM32_MAX_DATA_LEN + 1))) {
            s_diag.bad_length_errors++;
            state = PARSER_WAIT_HEAD0;
        } else {
            len = byte;
            pos = 0;
            state = PARSER_WAIT_BODY;
        }
        break;

    case PARSER_WAIT_BODY:
        body[pos++] = byte;
        if (pos >= len) {
            state = PARSER_WAIT_CHECKSUM;
        }
        break;

    case PARSER_WAIT_CHECKSUM:
    {
        uint8_t sum_data[STM32_MAX_DATA_LEN + 2];
        sum_data[0] = len;
        memcpy(&sum_data[1], body, len);
        if (checksum(sum_data, (uint8_t)(len + 1u)) == byte) {
            handle_status_frame(body[0], &body[1], (uint8_t)(len - 1u));
        } else {
            s_diag.checksum_errors++;
            ESP_LOGW(TAG, "STM32 checksum mismatch");
        }
        state = PARSER_WAIT_HEAD0;
        break;
    }

    default:
        state = PARSER_WAIT_HEAD0;
        break;
    }
}

static uint8_t move_cmd(stm32_link_move_t direction)
{
    switch (direction) {
    case STM32_MOVE_FORWARD:
        return CMD_MOVE_FORWARD;
    case STM32_MOVE_BACKWARD:
        return CMD_MOVE_BACKWARD;
    case STM32_MOVE_TURN_LEFT:
        return CMD_TURN_LEFT;
    case STM32_MOVE_TURN_RIGHT:
        return CMD_TURN_RIGHT;
    case STM32_MOVE_ROTATE_LEFT:
        return CMD_ROTATE_LEFT;
    case STM32_MOVE_ROTATE_RIGHT:
        return CMD_ROTATE_RIGHT;
    default:
        return CMD_STOP;
    }
}

static safety_move_direction_t to_safety_direction(stm32_link_move_t direction)
{
    return (safety_move_direction_t)direction;
}

static void stm32_link_task(void *arg)
{
    uint8_t rx[128];
    uint32_t last_heartbeat = 0;
    uint32_t last_status = 0;
    uint32_t last_log = 0;

    (void)arg;
    stm32_link_request_status();

    while (1) {
        uint32_t tick = now_ms();

        if ((tick - last_heartbeat) >= STM32_HEARTBEAT_MS) {
            last_heartbeat = tick;
            uint8_t seq = ++s_heartbeat_seq;
            send_frame(CMD_HEARTBEAT, &seq, 1);
        }

        if ((tick - last_status) >= STM32_STATUS_REQUEST_MS) {
            last_status = tick;
            stm32_link_request_status();
        }

        int got = uart_read_bytes(BOARD_STM32_UART_NUM, rx, sizeof(rx), pdMS_TO_TICKS(50));
        if (got > 0) {
            s_diag.rx_bytes += (uint32_t)got;
        }
        for (int i = 0; i < got; i++) {
            parser_input(rx[i]);
        }

        tick = now_ms();
        sensor_cache_snapshot_t cache = sensor_cache_get_snapshot();
        if (s_diag.online &&
            (s_diag.last_valid_frame_ms != 0u) &&
            ((tick - s_diag.last_valid_frame_ms) > STM32_OFFLINE_MS)) {
            s_diag.online = false;
            sensor_cache_set_online(false, tick);
            ESP_LOGW(TAG, "STM32 offline timeout: rx_bytes=%lu rx_frames=%lu ack=%lu checksum_err=%lu last_cmd=0x%02X age_ms=%lu",
                     (unsigned long)s_diag.rx_bytes,
                     (unsigned long)s_diag.rx_frames,
                     (unsigned long)s_diag.heartbeat_acks,
                     (unsigned long)s_diag.checksum_errors,
                     s_diag.last_rx_cmd,
                     (unsigned long)(tick - s_diag.last_valid_frame_ms));
        }

        if ((tick - last_log) >= 5000) {
            last_log = tick;
            cache = sensor_cache_get_snapshot();
            ESP_LOGI(TAG, "online=%d tx=%lu rx_bytes=%lu rx_frames=%lu ack=%lu last_cmd=0x%02X age_ms=%lu dist=%u temp=%u hum=%u motor=%u/%u gimbal=%u/%u behavior=%u",
                     cache.basic.stm32_online,
                     (unsigned long)s_diag.tx_frames,
                     (unsigned long)s_diag.rx_bytes,
                     (unsigned long)s_diag.rx_frames,
                     (unsigned long)s_diag.heartbeat_acks,
                     s_diag.last_rx_cmd,
                     (unsigned long)((s_diag.last_valid_frame_ms == 0u) ? 0u : (tick - s_diag.last_valid_frame_ms)),
                     cache.sensor.distance_cm_x10,
                     cache.sensor.temperature,
                     cache.sensor.humidity,
                     cache.motor.state,
                     cache.motor.current_speed,
                     cache.gimbal.pan,
                     cache.gimbal.tilt,
                     cache.behavior.behavior_id);
        }
    }
}

esp_err_t stm32_link_init(void)
{
    uart_config_t uart_cfg = {
        .baud_rate = BOARD_STM32_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    s_tx_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_tx_lock != NULL, ESP_ERR_NO_MEM, TAG, "tx mutex failed");

    ESP_RETURN_ON_ERROR(uart_driver_install(BOARD_STM32_UART_NUM, 4096, 4096, 0, NULL, 0), TAG, "uart_driver_install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(BOARD_STM32_UART_NUM, &uart_cfg), TAG, "uart_param_config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(BOARD_STM32_UART_NUM,
                                     BOARD_STM32_UART_TX_GPIO,
                                     BOARD_STM32_UART_RX_GPIO,
                                     UART_PIN_NO_CHANGE,
                                     UART_PIN_NO_CHANGE), TAG, "uart_set_pin failed");
    ESP_LOGI(TAG, "UART ready: num=%d tx=%d rx=%d baud=%d",
             BOARD_STM32_UART_NUM, BOARD_STM32_UART_TX_GPIO, BOARD_STM32_UART_RX_GPIO, BOARD_STM32_UART_BAUDRATE);
    return ESP_OK;
}

esp_err_t stm32_link_start(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreate(stm32_link_task, "stm32_link", 4096, NULL, 6, &s_task);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool stm32_link_is_online(void)
{
    return safety_guard_is_stm32_online();
}

void stm32_link_get_diagnostics(stm32_link_diag_t *out)
{
    if (out == NULL) {
        return;
    }

    *out = s_diag;
    out->online = safety_guard_is_stm32_online();
}

esp_err_t stm32_link_request_status(void)
{
    return send_frame(CMD_REQUEST_STATUS, NULL, 0);
}

esp_err_t stm32_link_stop(void)
{
    return send_control_frame(CMD_STOP, NULL, 0);
}

esp_err_t stm32_link_move(stm32_link_move_t direction, uint8_t speed, uint16_t duration_ms)
{
    uint8_t payload[3];
    uint8_t safe_speed = safety_guard_clamp_speed(speed);
    esp_err_t safe = safety_guard_check_move(to_safety_direction(direction), safe_speed);
    if (safe != ESP_OK) {
        ESP_LOGW(TAG, "move rejected by safety_guard: dir=%d speed=%u", direction, safe_speed);
        return safe;
    }

    payload[0] = safe_speed;
    put_u16_le(&payload[1], duration_ms);
    return send_control_frame(move_cmd(direction), payload, sizeof(payload));
}

esp_err_t stm32_link_set_speed(uint8_t speed)
{
    uint8_t safe_speed = safety_guard_clamp_speed(speed);
    return send_control_frame(CMD_SET_SPEED, &safe_speed, 1);
}

esp_err_t stm32_link_gimbal_center(uint16_t duration_ms)
{
    uint8_t payload[2];
    put_u16_le(payload, duration_ms);
    return send_control_frame(CMD_GIMBAL_CENTER, payload, sizeof(payload));
}

esp_err_t stm32_link_gimbal_set(uint8_t pan, uint8_t tilt, uint16_t duration_ms, stm32_link_easing_t easing)
{
    uint8_t payload[5];
    payload[0] = pan;
    payload[1] = safety_guard_clamp_tilt(tilt);
    put_u16_le(&payload[2], duration_ms);
    payload[4] = (uint8_t)easing;
    return send_control_frame(CMD_SET_PAN_TILT, payload, sizeof(payload));
}

esp_err_t stm32_link_gimbal_action(uint8_t action_id)
{
    return send_control_frame(CMD_GIMBAL_ACTION, &action_id, 1);
}

esp_err_t stm32_link_led_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t payload[3] = {r, g, b};
    return send_control_frame(CMD_LED_RGB, payload, sizeof(payload));
}

esp_err_t stm32_link_led_effect(uint8_t effect_id, uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms)
{
    uint8_t payload[6];
    payload[0] = effect_id;
    payload[1] = r;
    payload[2] = g;
    payload[3] = b;
    put_u16_le(&payload[4], duration_ms);
    return send_control_frame(CMD_LED_EFFECT, payload, sizeof(payload));
}

esp_err_t stm32_link_behavior_start(uint8_t behavior_id)
{
    return send_control_frame(CMD_BEHAVIOR_START, &behavior_id, 1);
}

esp_err_t stm32_link_behavior_stop(void)
{
    return send_control_frame(CMD_BEHAVIOR_STOP, NULL, 0);
}
