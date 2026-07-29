#pragma once

/*
 * ESP32 high-level behavior scheduler.
 * Only patrol and dance are exposed as high-level behaviors. STM32 still owns
 * hard real-time motor/gimbal safety.
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BEHAVIOR_MANAGER_IDLE = 0,
    BEHAVIOR_MANAGER_DANCE,
    BEHAVIOR_MANAGER_PATROL,
} behavior_manager_mode_t;

typedef struct {
    behavior_manager_mode_t mode;
    const char *mode_name;
    uint8_t step;
    bool running;
    uint32_t step_elapsed_ms;
    uint32_t loops;
    uint8_t vision_failures;
    char last_decision[16];
    char last_error[32];
} behavior_manager_status_t;

esp_err_t behavior_manager_init(void);
esp_err_t behavior_manager_start(const char *name);
esp_err_t behavior_manager_stop(void);
esp_err_t behavior_manager_apply_vision_decision(const char *decision);
void behavior_manager_update(uint32_t now_ms);
void behavior_manager_get_status(behavior_manager_status_t *out);
const char *behavior_manager_mode_name(behavior_manager_mode_t mode);
bool behavior_manager_is_patrol_running(void);
