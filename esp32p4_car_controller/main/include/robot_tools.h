#pragma once

/*
 * 模块：robot_tools
 * 负责什么：给 Qwen Function Calling 和 Web 页面提供安全的高层机器人动作工具。
 * 用到什么硬件：通过 stm32_link 间接控制 STM32，不直接控制 PWM/GPIO。
 * 初始化函数：robot_tools_init()。
 * 主循环调用：不需要，收到工具调用或网页请求时调用。
 * 怎么测试：调用 robot_tools_behavior("patrol") 或 robot_tools_behavior("dance")。
 * 失败先检查：stm32_link 是否在线、safety_guard 是否拒绝了危险移动。
 */

#include "esp_err.h"
#include "sensor_cache.h"
#include <stdint.h>

esp_err_t robot_tools_init(void);
sensor_cache_snapshot_t robot_tools_get_status(void);
esp_err_t robot_tools_stop(void);
esp_err_t robot_tools_move(const char *direction, uint8_t speed, uint16_t duration_ms);
esp_err_t robot_tools_gimbal_set(uint8_t pan, uint8_t tilt, uint16_t duration_ms);
esp_err_t robot_tools_gimbal_action(const char *action_name);
esp_err_t robot_tools_led_effect(const char *effect, uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms);
esp_err_t robot_tools_behavior(const char *name);
esp_err_t robot_tools_capture_image(void);
