#include "behavior_manager.h"

#include "esp_log.h"
#include "image_sampler.h"
#include "qwen_realtime_client.h"
#include "sensor_cache.h"
#include "stm32_link.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "behavior_mgr";

#define PATROL_SPEED 25u
#define PATROL_ROTATE_SPEED 25u
#define PATROL_OBSTACLE_CM_X10 150u
#define PATROL_STEP_SETTLE_MS 220u
#define PATROL_GIMBAL_MOVE_MS 650u
#define PATROL_GIMBAL_SETTLE_MS 800u
#define PATROL_AI_DECISION_WAIT_MS 6500u
#define PATROL_ROTATE_MS 1200u
#define PATROL_ROTATE_SETTLE_MS 1400u

typedef enum {
    PATROL_STEP_DRIVE = 0,
    PATROL_STEP_STOPPING,
    PATROL_STEP_LOOK_LEFT,
    PATROL_STEP_PHOTO_LEFT,
    PATROL_STEP_LOOK_FRONT,
    PATROL_STEP_PHOTO_FRONT,
    PATROL_STEP_LOOK_RIGHT,
    PATROL_STEP_PHOTO_RIGHT,
    PATROL_STEP_CENTER_AND_ASK_AI,
    PATROL_STEP_WAIT_AI,
    PATROL_STEP_ROTATE,
} patrol_step_t;

typedef struct {
    behavior_manager_mode_t mode;
    uint8_t step;
    uint32_t step_start_ms;
    uint32_t last_action_ms;
    uint32_t loops;
    uint8_t vision_failures;
    bool vision_decision_ready;
    stm32_link_move_t rotate_direction;
    char vision_decision[16];
    char last_decision[16];
    char last_error[32];
} behavior_state_t;

static behavior_state_t s_state;

static bool str_eq(const char *a, const char *b)
{
    return (a != NULL) && (b != NULL) && (strcmp(a, b) == 0);
}

const char *behavior_manager_mode_name(behavior_manager_mode_t mode)
{
    switch (mode) {
    case BEHAVIOR_MANAGER_IDLE:
        return "idle";
    case BEHAVIOR_MANAGER_DANCE:
        return "dance";
    case BEHAVIOR_MANAGER_PATROL:
        return "patrol";
    default:
        return "unknown";
    }
}

bool behavior_manager_is_patrol_running(void)
{
    return s_state.mode == BEHAVIOR_MANAGER_PATROL;
}

static uint32_t elapsed(uint32_t now_ms)
{
    return now_ms - s_state.step_start_ms;
}

static void set_error(esp_err_t ret)
{
    if (ret == ESP_OK) {
        s_state.last_error[0] = '\0';
    } else {
        snprintf(s_state.last_error, sizeof(s_state.last_error), "%s", esp_err_to_name(ret));
    }
}

static esp_err_t send_prompt_if_ready(const char *text)
{
    if (!qwen_realtime_client_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    return qwen_realtime_client_send_internal_text(text);
}

static void enter_step(uint8_t step, uint32_t now_ms)
{
    s_state.step = step;
    s_state.step_start_ms = now_ms;
}

static void enter_mode(behavior_manager_mode_t mode, uint32_t now_ms)
{
    s_state.mode = mode;
    s_state.step = 0;
    s_state.step_start_ms = now_ms;
    s_state.last_action_ms = 0;
    s_state.vision_failures = 0;
    s_state.vision_decision_ready = false;
    s_state.rotate_direction = STM32_MOVE_ROTATE_RIGHT;
    s_state.vision_decision[0] = '\0';
    s_state.last_decision[0] = '\0';
    s_state.last_error[0] = '\0';
    ESP_LOGI(TAG, "enter behavior mode=%s", behavior_manager_mode_name(mode));
}

static bool front_is_blocked(void)
{
    sensor_cache_snapshot_t st = sensor_cache_get_snapshot();
    if (st.sensor.distance_cm_x10 == 0xFFFFu) {
        return false;
    }
    return st.sensor.distance_cm_x10 < PATROL_OBSTACLE_CM_X10;
}

static void stop_motion_and_images(void)
{
    image_sampler_stop();
    stm32_link_stop();
}

esp_err_t behavior_manager_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.mode = BEHAVIOR_MANAGER_IDLE;
    s_state.rotate_direction = STM32_MOVE_ROTATE_RIGHT;
    snprintf(s_state.last_decision, sizeof(s_state.last_decision), "none");
    return ESP_OK;
}

esp_err_t behavior_manager_stop(void)
{
    stop_motion_and_images();
    stm32_link_led_effect(0, 0, 0, 0, 0);
    s_state.mode = BEHAVIOR_MANAGER_IDLE;
    s_state.step = 0;
    s_state.vision_decision_ready = false;
    snprintf(s_state.last_decision, sizeof(s_state.last_decision), "stopped");
    ESP_LOGI(TAG, "behavior stopped");
    return ESP_OK;
}

esp_err_t behavior_manager_start(const char *name)
{
    uint32_t now_ms = (uint32_t)(esp_log_timestamp());
    esp_err_t ret = ESP_OK;

    if (str_eq(name, "stop_patrol") || str_eq(name, "stop") || str_eq(name, "idle")) {
        return behavior_manager_stop();
    }

    if (str_eq(name, "dance")) {
        stop_motion_and_images();
        stm32_link_led_effect(5, 0, 0, 0, 0);
        stm32_link_gimbal_action(13);
        enter_mode(BEHAVIOR_MANAGER_DANCE, now_ms);
    } else if (str_eq(name, "patrol")) {
        stop_motion_and_images();
        stm32_link_led_effect(6, 0, 0, 0, 0);
        stm32_link_gimbal_center(600);
        enter_mode(BEHAVIOR_MANAGER_PATROL, now_ms);
    } else {
        ret = ESP_ERR_NOT_SUPPORTED;
    }

    set_error(ret);
    return ret;
}

esp_err_t behavior_manager_apply_vision_decision(const char *decision)
{
    if ((decision == NULL) || (decision[0] == '\0')) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((s_state.mode != BEHAVIOR_MANAGER_PATROL) || (s_state.step != PATROL_STEP_WAIT_AI)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!str_eq(decision, "left") && !str_eq(decision, "right")) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(s_state.vision_decision, sizeof(s_state.vision_decision), "%s", decision);
    snprintf(s_state.last_decision, sizeof(s_state.last_decision), "%s", decision);
    s_state.rotate_direction = str_eq(decision, "left") ? STM32_MOVE_ROTATE_LEFT : STM32_MOVE_ROTATE_RIGHT;
    s_state.vision_decision_ready = true;
    ESP_LOGI(TAG, "AI patrol vision decision=%s", decision);
    return ESP_OK;
}

static void update_dance(uint32_t now_ms)
{
    switch (s_state.step) {
    case 0:
        if (elapsed(now_ms) >= 900u) {
            set_error(stm32_link_move(STM32_MOVE_ROTATE_LEFT, 30, 1200));
            enter_step(1, now_ms);
        }
        break;
    case 1:
        if (elapsed(now_ms) >= 1450u) {
            stm32_link_gimbal_action(13);
            set_error(stm32_link_move(STM32_MOVE_ROTATE_RIGHT, 30, 1200));
            enter_step(2, now_ms);
        }
        break;
    case 2:
        if (elapsed(now_ms) >= 1450u) {
            set_error(stm32_link_move(STM32_MOVE_FORWARD, 25, 1200));
            enter_step(3, now_ms);
        }
        break;
    case 3:
        if (elapsed(now_ms) >= 1350u) {
            set_error(stm32_link_move(STM32_MOVE_BACKWARD, 25, 1200));
            enter_step(4, now_ms);
        }
        break;
    case 4:
        if (elapsed(now_ms) >= 1350u) {
            stm32_link_gimbal_action(3);
            stm32_link_led_effect(0, 0, 0, 0, 0);
            enter_step(5, now_ms);
        }
        break;
    case 5:
        if (elapsed(now_ms) >= 1100u) {
            stm32_link_gimbal_center(600);
            behavior_manager_stop();
        }
        break;
    default:
        behavior_manager_stop();
        break;
    }
}

static void patrol_drive(uint32_t now_ms)
{
    if (front_is_blocked()) {
        stm32_link_stop();
        stm32_link_led_effect(3, 0, 0, 0, 0);
        image_sampler_stop();
        snprintf(s_state.last_decision, sizeof(s_state.last_decision), "scanning");
        enter_step(PATROL_STEP_STOPPING, now_ms);
        return;
    }

    if (s_state.last_action_ms == 0u) {
        set_error(stm32_link_move(STM32_MOVE_FORWARD, PATROL_SPEED, 0));
        s_state.last_action_ms = now_ms;
        s_state.loops++;
    }
}

static void patrol_capture_sequence(uint32_t now_ms)
{
    switch ((patrol_step_t)s_state.step) {
    case PATROL_STEP_STOPPING:
        if (elapsed(now_ms) >= PATROL_STEP_SETTLE_MS) {
            stm32_link_gimbal_set(45, 90, PATROL_GIMBAL_MOVE_MS, STM32_EASING_IN_OUT);
            enter_step(PATROL_STEP_LOOK_LEFT, now_ms);
        }
        break;
    case PATROL_STEP_LOOK_LEFT:
        if (elapsed(now_ms) >= PATROL_GIMBAL_SETTLE_MS) {
            image_sampler_request_once(IMAGE_SAMPLER_REASON_PATROL);
            enter_step(PATROL_STEP_PHOTO_LEFT, now_ms);
        }
        break;
    case PATROL_STEP_PHOTO_LEFT:
        if (elapsed(now_ms) >= 850u) {
            stm32_link_gimbal_set(90, 90, PATROL_GIMBAL_MOVE_MS, STM32_EASING_IN_OUT);
            enter_step(PATROL_STEP_LOOK_FRONT, now_ms);
        }
        break;
    case PATROL_STEP_LOOK_FRONT:
        if (elapsed(now_ms) >= PATROL_GIMBAL_SETTLE_MS) {
            image_sampler_request_once(IMAGE_SAMPLER_REASON_PATROL);
            enter_step(PATROL_STEP_PHOTO_FRONT, now_ms);
        }
        break;
    case PATROL_STEP_PHOTO_FRONT:
        if (elapsed(now_ms) >= 850u) {
            stm32_link_gimbal_set(135, 90, PATROL_GIMBAL_MOVE_MS, STM32_EASING_IN_OUT);
            enter_step(PATROL_STEP_LOOK_RIGHT, now_ms);
        }
        break;
    case PATROL_STEP_LOOK_RIGHT:
        if (elapsed(now_ms) >= PATROL_GIMBAL_SETTLE_MS) {
            image_sampler_request_once(IMAGE_SAMPLER_REASON_PATROL);
            enter_step(PATROL_STEP_PHOTO_RIGHT, now_ms);
        }
        break;
    case PATROL_STEP_PHOTO_RIGHT:
        if (elapsed(now_ms) >= 850u) {
            stm32_link_gimbal_center(600);
            enter_step(PATROL_STEP_CENTER_AND_ASK_AI, now_ms);
        }
        break;
    case PATROL_STEP_CENTER_AND_ASK_AI:
        if (elapsed(now_ms) >= 700u) {
            s_state.vision_decision_ready = false;
            s_state.vision_decision[0] = '\0';
            esp_err_t decision_ret = send_prompt_if_ready(
                "Patrol obstacle scan complete. Three images were captured in this exact order: left, front, right. "
                "Reply in Simplified Chinese and clearly say what is on the left, front, and right. "
                "You MUST call robot_patrol_decision with decision=left or decision=right only. "
                "Choose the safer turning direction for the car.");
            if (decision_ret != ESP_OK) {
                s_state.vision_failures++;
            }
            snprintf(s_state.last_decision, sizeof(s_state.last_decision), "%s", (decision_ret == ESP_OK) ? "ai_wait" : "fallback");
            enter_step(PATROL_STEP_WAIT_AI, now_ms);
        }
        break;
    default:
        break;
    }
}

static void patrol_wait_ai_or_rotate(uint32_t now_ms)
{
    if (s_state.step == PATROL_STEP_WAIT_AI) {
        if (s_state.vision_decision_ready || (elapsed(now_ms) >= PATROL_AI_DECISION_WAIT_MS)) {
            if (!s_state.vision_decision_ready) {
                s_state.rotate_direction = STM32_MOVE_ROTATE_RIGHT;
                snprintf(s_state.last_decision, sizeof(s_state.last_decision), "fallback_right");
            }
            set_error(stm32_link_move(s_state.rotate_direction, PATROL_ROTATE_SPEED, PATROL_ROTATE_MS));
            enter_step(PATROL_STEP_ROTATE, now_ms);
        }
        return;
    }

    if ((s_state.step == PATROL_STEP_ROTATE) && (elapsed(now_ms) >= PATROL_ROTATE_SETTLE_MS)) {
        if (front_is_blocked()) {
            set_error(stm32_link_move(s_state.rotate_direction, PATROL_ROTATE_SPEED, PATROL_ROTATE_MS));
            enter_step(PATROL_STEP_ROTATE, now_ms);
        } else {
            stm32_link_led_effect(6, 0, 0, 0, 0);
            s_state.last_action_ms = 0;
            enter_step(PATROL_STEP_DRIVE, now_ms);
        }
    }
}

static void update_patrol(uint32_t now_ms)
{
    switch ((patrol_step_t)s_state.step) {
    case PATROL_STEP_DRIVE:
        patrol_drive(now_ms);
        break;
    case PATROL_STEP_STOPPING:
    case PATROL_STEP_LOOK_LEFT:
    case PATROL_STEP_PHOTO_LEFT:
    case PATROL_STEP_LOOK_FRONT:
    case PATROL_STEP_PHOTO_FRONT:
    case PATROL_STEP_LOOK_RIGHT:
    case PATROL_STEP_PHOTO_RIGHT:
    case PATROL_STEP_CENTER_AND_ASK_AI:
        patrol_capture_sequence(now_ms);
        break;
    case PATROL_STEP_WAIT_AI:
    case PATROL_STEP_ROTATE:
        patrol_wait_ai_or_rotate(now_ms);
        break;
    default:
        enter_step(PATROL_STEP_DRIVE, now_ms);
        break;
    }
}

void behavior_manager_update(uint32_t now_ms)
{
    switch (s_state.mode) {
    case BEHAVIOR_MANAGER_DANCE:
        update_dance(now_ms);
        break;
    case BEHAVIOR_MANAGER_PATROL:
        update_patrol(now_ms);
        break;
    case BEHAVIOR_MANAGER_IDLE:
    default:
        break;
    }

    if ((s_state.mode != BEHAVIOR_MANAGER_IDLE) && front_is_blocked() &&
        ((s_state.mode != BEHAVIOR_MANAGER_PATROL) || (s_state.step == PATROL_STEP_DRIVE))) {
        stm32_link_stop();
    }
}

void behavior_manager_get_status(behavior_manager_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->mode = s_state.mode;
    out->mode_name = behavior_manager_mode_name(s_state.mode);
    out->step = s_state.step;
    out->running = s_state.mode != BEHAVIOR_MANAGER_IDLE;
    out->step_elapsed_ms = (uint32_t)(esp_log_timestamp()) - s_state.step_start_ms;
    out->loops = s_state.loops;
    out->vision_failures = s_state.vision_failures;
    snprintf(out->last_decision, sizeof(out->last_decision), "%s", s_state.last_decision);
    snprintf(out->last_error, sizeof(out->last_error), "%s", s_state.last_error);
}
