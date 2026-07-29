#pragma once

/*
 * 模块：safety_guard
 * 负责什么：ESP32 侧二次安全限制，避免 AI 或网页发出危险动作。
 * 用到什么硬件：不直接操作硬件，读取 sensor_cache 的状态并限制即将发给 STM32 的命令。
 * 初始化函数：safety_guard_init()。
 * 主循环调用：不需要，robot_tools 和 stm32_link 发命令前调用。
 * 怎么测试：障碍距离小于 15 cm 或 STM32 离线时，前进命令应被拒绝。
 * 失败先检查：sensor_cache 是否已收到 STM32 的 0x80/0x81 状态帧。
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define SAFETY_GUARD_MAX_SPEED_PERCENT 60
#define SAFETY_GUARD_TILT_MIN_DEG 30
#define SAFETY_GUARD_TILT_MAX_DEG 150
#define SAFETY_GUARD_OBSTACLE_STOP_CM_X10 150

typedef enum {
    SAFETY_MOVE_FORWARD = 0,
    SAFETY_MOVE_BACKWARD,
    SAFETY_MOVE_TURN_LEFT,
    SAFETY_MOVE_TURN_RIGHT,
    SAFETY_MOVE_ROTATE_LEFT,
    SAFETY_MOVE_ROTATE_RIGHT,
} safety_move_direction_t;

esp_err_t safety_guard_init(void);
uint8_t safety_guard_clamp_speed(uint8_t speed);
uint8_t safety_guard_clamp_tilt(uint8_t tilt);
bool safety_guard_is_stm32_online(void);
bool safety_guard_is_obstacle_near(void);
esp_err_t safety_guard_check_move(safety_move_direction_t direction, uint8_t speed);
