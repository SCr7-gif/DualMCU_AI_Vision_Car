#include "robot_tools.h"

#include "behavior_manager.h"
#include "esp_log.h"
#include "image_sampler.h"
#include "stm32_link.h"
#include <string.h>

static const char *TAG = "robot_tools";

static bool str_eq(const char *a, const char *b)
{
    return (a != NULL) && (b != NULL) && (strcmp(a, b) == 0);
}

static esp_err_t parse_direction(const char *direction, stm32_link_move_t *out)
{
    if (str_eq(direction, "forward")) {
        *out = STM32_MOVE_FORWARD;
    } else if (str_eq(direction, "backward")) {
        *out = STM32_MOVE_BACKWARD;
    } else if (str_eq(direction, "left") || str_eq(direction, "turn_left")) {
        *out = STM32_MOVE_TURN_LEFT;
    } else if (str_eq(direction, "right") || str_eq(direction, "turn_right")) {
        *out = STM32_MOVE_TURN_RIGHT;
    } else if (str_eq(direction, "rotate_left")) {
        *out = STM32_MOVE_ROTATE_LEFT;
    } else if (str_eq(direction, "rotate_right")) {
        *out = STM32_MOVE_ROTATE_RIGHT;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t gimbal_action_id(const char *name, uint8_t *out)
{
    if (str_eq(name, "center")) {
        *out = 0;
    } else if (str_eq(name, "look_left_soft")) {
        *out = 1;
    } else if (str_eq(name, "look_right_soft")) {
        *out = 2;
    } else if (str_eq(name, "nod_yes")) {
        *out = 3;
    } else if (str_eq(name, "shake_no")) {
        *out = 4;
    } else if (str_eq(name, "scan_slow")) {
        *out = 5;
    } else if (str_eq(name, "curious_tilt")) {
        *out = 6;
    } else if (str_eq(name, "look_up_soft")) {
        *out = 7;
    } else if (str_eq(name, "look_down_soft")) {
        *out = 8;
    } else if (str_eq(name, "scan_fast")) {
        *out = 9;
    } else if (str_eq(name, "sleepy") || str_eq(name, "sleep_pose")) {
        *out = 10;
    } else if (str_eq(name, "wake_up")) {
        *out = 11;
    } else if (str_eq(name, "shy_look_down")) {
        *out = 12;
    } else if (str_eq(name, "happy_wiggle")) {
        *out = 13;
    } else if (str_eq(name, "thinking_motion")) {
        *out = 14;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t led_effect_id(const char *name, uint8_t *out)
{
    if (str_eq(name, "idle")) {
        *out = 0;
    } else if (str_eq(name, "listening")) {
        *out = 1;
    } else if (str_eq(name, "speaking")) {
        *out = 2;
    } else if (str_eq(name, "thinking")) {
        *out = 3;
    } else if (str_eq(name, "error")) {
        *out = 4;
    } else if (str_eq(name, "cute")) {
        *out = 5;
    } else if (str_eq(name, "patrol")) {
        *out = 6;
    } else if (str_eq(name, "solid")) {
        *out = 7;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static bool patrol_locked(void)
{
    return behavior_manager_is_patrol_running();
}

static esp_err_t reject_while_patrolling(const char *tool)
{
    if (!patrol_locked()) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "%s ignored while patrol is running; use robot_behavior stop_patrol first", tool);
    return ESP_ERR_INVALID_STATE;
}

esp_err_t robot_tools_init(void)
{
    ESP_LOGI(TAG, "robot tools ready");
    return ESP_OK;
}

sensor_cache_snapshot_t robot_tools_get_status(void)
{
    return sensor_cache_get_snapshot();
}

esp_err_t robot_tools_stop(void)
{
    esp_err_t guard = reject_while_patrolling("robot_stop");
    if (guard != ESP_OK) {
        return guard;
    }

    ESP_LOGI(TAG, "tool robot_stop");
    return behavior_manager_stop();
}

esp_err_t robot_tools_move(const char *direction, uint8_t speed, uint16_t duration_ms)
{
    stm32_link_move_t dir;
    esp_err_t guard = reject_while_patrolling("robot_move");
    if (guard != ESP_OK) {
        return guard;
    }

    esp_err_t ret = parse_direction(direction, &dir);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "bad direction: %s", direction ? direction : "NULL");
        return ret;
    }

    ESP_LOGI(TAG, "tool robot_move direction=%s speed=%u duration=%u", direction, speed, duration_ms);
    behavior_manager_stop();
    return stm32_link_move(dir, speed, duration_ms);
}

esp_err_t robot_tools_gimbal_set(uint8_t pan, uint8_t tilt, uint16_t duration_ms)
{
    esp_err_t guard = reject_while_patrolling("robot_gimbal_set");
    if (guard != ESP_OK) {
        return guard;
    }

    ESP_LOGI(TAG, "tool robot_gimbal_set pan=%u tilt=%u duration=%u", pan, tilt, duration_ms);
    behavior_manager_stop();
    return stm32_link_gimbal_set(pan, tilt, duration_ms, STM32_EASING_IN_OUT);
}

esp_err_t robot_tools_gimbal_action(const char *action_name)
{
    uint8_t id;
    esp_err_t guard = reject_while_patrolling("robot_gimbal_action");
    if (guard != ESP_OK) {
        return guard;
    }

    esp_err_t ret = gimbal_action_id(action_name, &id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "bad gimbal action: %s", action_name ? action_name : "NULL");
        return ret;
    }

    ESP_LOGI(TAG, "tool robot_gimbal_action %s", action_name);
    behavior_manager_stop();
    return stm32_link_gimbal_action(id);
}

esp_err_t robot_tools_led_effect(const char *effect, uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms)
{
    uint8_t id;
    esp_err_t ret = led_effect_id(effect, &id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "bad led effect: %s", effect ? effect : "NULL");
        return ret;
    }

    ESP_LOGI(TAG, "tool robot_led_effect %s", effect);
    return stm32_link_led_effect(id, r, g, b, duration_ms);
}

esp_err_t robot_tools_capture_image(void)
{
    esp_err_t guard = reject_while_patrolling("robot_capture_image");
    if (guard != ESP_OK) {
        return guard;
    }

    ESP_LOGI(TAG, "tool robot_capture_image");
    image_sampler_request_once(IMAGE_SAMPLER_REASON_LOOK_AROUND);
    return ESP_OK;
}

esp_err_t robot_tools_behavior(const char *name)
{
    if (patrol_locked()) {
        if (str_eq(name, "stop_patrol")) {
            ESP_LOGI(TAG, "tool robot_behavior stop_patrol");
            return behavior_manager_start(name);
        }
        if (str_eq(name, "patrol")) {
            ESP_LOGI(TAG, "tool robot_behavior patrol already running");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "behavior %s ignored while patrol is running; use stop_patrol first", name ? name : "NULL");
        return ESP_ERR_INVALID_STATE;
    }

    if (str_eq(name, "dance") || str_eq(name, "patrol") ||
        str_eq(name, "stop_patrol") || str_eq(name, "stop") || str_eq(name, "idle")) {
        ESP_LOGI(TAG, "tool robot_behavior %s", name);
        return behavior_manager_start(name);
    }

    ESP_LOGW(TAG, "behavior not supported yet: %s", name ? name : "NULL");
    return ESP_ERR_NOT_SUPPORTED;
}
