#pragma once

/*
 * 模块：sensor_cache
 * 负责什么：缓存 STM32 上报的电机、云台、传感器、GPS、行为和在线状态。
 * 用到什么硬件：不直接操作硬件，只保存 stm32_link 解析出来的数据。
 * 初始化函数：sensor_cache_init()。
 * 主循环调用：不需要主循环调用，其他模块按需读写。
 * 怎么测试：STM32 正常上报状态后，日志里能看到距离、云台、电机和行为状态变化。
 * 失败先检查：STM32 UART 是否在线、状态帧命令号和 payload 长度是否正确。
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define SENSOR_CACHE_GPS_MAX_LEN 65

typedef struct {
    uint8_t flags;
    uint16_t uptime_ms_low16;
    bool stm32_online;
    uint32_t last_rx_ms;
} sensor_cache_basic_t;

typedef struct {
    uint16_t distance_cm_x10;
    uint8_t temperature;
    uint8_t humidity;
    bool dht_ok;
} sensor_cache_sensor_t;

typedef struct {
    uint8_t state;
    uint8_t target_speed;
    uint8_t current_speed;
} sensor_cache_motor_t;

typedef struct {
    uint8_t pan;
    uint8_t tilt;
    bool moving;
    uint8_t action_id;
} sensor_cache_gimbal_t;

typedef struct {
    uint8_t behavior_id;
    bool running;
    uint8_t step;
} sensor_cache_behavior_t;

typedef struct {
    sensor_cache_basic_t basic;
    sensor_cache_sensor_t sensor;
    sensor_cache_motor_t motor;
    sensor_cache_gimbal_t gimbal;
    sensor_cache_behavior_t behavior;
    char gps_nmea[SENSOR_CACHE_GPS_MAX_LEN];
} sensor_cache_snapshot_t;

esp_err_t sensor_cache_init(void);
void sensor_cache_set_online(bool online, uint32_t now_ms);
void sensor_cache_update_basic(uint8_t flags, uint16_t uptime_ms_low16, uint32_t now_ms);
void sensor_cache_update_sensor(uint16_t distance_cm_x10, uint8_t temperature, uint8_t humidity, bool dht_ok, uint32_t now_ms);
void sensor_cache_update_motor(uint8_t state, uint8_t target_speed, uint8_t current_speed, uint32_t now_ms);
void sensor_cache_update_gimbal(uint8_t pan, uint8_t tilt, bool moving, uint8_t action_id, uint32_t now_ms);
void sensor_cache_update_behavior(uint8_t behavior_id, bool running, uint8_t step, uint32_t now_ms);
void sensor_cache_update_gps(const uint8_t *data, uint8_t len, uint32_t now_ms);
sensor_cache_snapshot_t sensor_cache_get_snapshot(void);
