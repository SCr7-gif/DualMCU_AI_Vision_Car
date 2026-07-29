#include "safety_guard.h"

#include "esp_err.h"
#include "sensor_cache.h"

esp_err_t safety_guard_init(void)
{
    return ESP_OK;
}

uint8_t safety_guard_clamp_speed(uint8_t speed)
{
    return (speed > SAFETY_GUARD_MAX_SPEED_PERCENT) ? SAFETY_GUARD_MAX_SPEED_PERCENT : speed;
}

uint8_t safety_guard_clamp_tilt(uint8_t tilt)
{
    if (tilt < SAFETY_GUARD_TILT_MIN_DEG) {
        return SAFETY_GUARD_TILT_MIN_DEG;
    }
    if (tilt > SAFETY_GUARD_TILT_MAX_DEG) {
        return SAFETY_GUARD_TILT_MAX_DEG;
    }
    return tilt;
}

bool safety_guard_is_stm32_online(void)
{
    sensor_cache_snapshot_t cache = sensor_cache_get_snapshot();
    return cache.basic.stm32_online;
}

bool safety_guard_is_obstacle_near(void)
{
    sensor_cache_snapshot_t cache = sensor_cache_get_snapshot();
    if (cache.sensor.distance_cm_x10 == 0xFFFFu) {
        return false;
    }
    return cache.sensor.distance_cm_x10 < SAFETY_GUARD_OBSTACLE_STOP_CM_X10;
}

esp_err_t safety_guard_check_move(safety_move_direction_t direction, uint8_t speed)
{
    if (speed == 0) {
        return ESP_OK;
    }

    if (!safety_guard_is_stm32_online()) {
        return ESP_ERR_INVALID_STATE;
    }

    if ((direction == SAFETY_MOVE_FORWARD) && safety_guard_is_obstacle_near()) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}
