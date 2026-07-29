#include "sensor_cache.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static sensor_cache_snapshot_t s_cache;
static SemaphoreHandle_t s_lock;

static void lock_cache(void)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock_cache(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static void mark_rx(uint32_t now_ms)
{
    s_cache.basic.stm32_online = true;
    s_cache.basic.last_rx_ms = now_ms;
}

esp_err_t sensor_cache_init(void)
{
    memset(&s_cache, 0, sizeof(s_cache));
    s_cache.sensor.distance_cm_x10 = 0xFFFFu;
    s_cache.gimbal.pan = 90;
    s_cache.gimbal.tilt = 90;

    s_lock = xSemaphoreCreateMutex();
    return (s_lock != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

void sensor_cache_set_online(bool online, uint32_t now_ms)
{
    lock_cache();
    s_cache.basic.stm32_online = online;
    if (online) {
        s_cache.basic.last_rx_ms = now_ms;
    }
    unlock_cache();
}

void sensor_cache_update_basic(uint8_t flags, uint16_t uptime_ms_low16, uint32_t now_ms)
{
    lock_cache();
    s_cache.basic.flags = flags;
    s_cache.basic.uptime_ms_low16 = uptime_ms_low16;
    mark_rx(now_ms);
    unlock_cache();
}

void sensor_cache_update_sensor(uint16_t distance_cm_x10, uint8_t temperature, uint8_t humidity, bool dht_ok, uint32_t now_ms)
{
    lock_cache();
    s_cache.sensor.distance_cm_x10 = distance_cm_x10;
    s_cache.sensor.temperature = temperature;
    s_cache.sensor.humidity = humidity;
    s_cache.sensor.dht_ok = dht_ok;
    mark_rx(now_ms);
    unlock_cache();
}

void sensor_cache_update_motor(uint8_t state, uint8_t target_speed, uint8_t current_speed, uint32_t now_ms)
{
    lock_cache();
    s_cache.motor.state = state;
    s_cache.motor.target_speed = target_speed;
    s_cache.motor.current_speed = current_speed;
    mark_rx(now_ms);
    unlock_cache();
}

void sensor_cache_update_gimbal(uint8_t pan, uint8_t tilt, bool moving, uint8_t action_id, uint32_t now_ms)
{
    lock_cache();
    s_cache.gimbal.pan = pan;
    s_cache.gimbal.tilt = tilt;
    s_cache.gimbal.moving = moving;
    s_cache.gimbal.action_id = action_id;
    mark_rx(now_ms);
    unlock_cache();
}

void sensor_cache_update_behavior(uint8_t behavior_id, bool running, uint8_t step, uint32_t now_ms)
{
    lock_cache();
    s_cache.behavior.behavior_id = behavior_id;
    s_cache.behavior.running = running;
    s_cache.behavior.step = step;
    mark_rx(now_ms);
    unlock_cache();
}

void sensor_cache_update_gps(const uint8_t *data, uint8_t len, uint32_t now_ms)
{
    uint8_t copy_len = len;
    if (copy_len >= SENSOR_CACHE_GPS_MAX_LEN) {
        copy_len = SENSOR_CACHE_GPS_MAX_LEN - 1;
    }

    lock_cache();
    if ((data != NULL) && (copy_len > 0)) {
        memcpy(s_cache.gps_nmea, data, copy_len);
    }
    s_cache.gps_nmea[copy_len] = '\0';
    mark_rx(now_ms);
    unlock_cache();
}

sensor_cache_snapshot_t sensor_cache_get_snapshot(void)
{
    sensor_cache_snapshot_t out;
    lock_cache();
    out = s_cache;
    unlock_cache();
    return out;
}
