#pragma once

/*
 * 模块：stm32_link
 * 负责什么：ESP32-P4 和 STM32 的正式 UART 协议收发。
 * 用到什么硬件：UART1，TX=GPIO47 接 STM32 PA10，RX=GPIO48 接 STM32 PA9。
 * 初始化函数：stm32_link_init()。
 * 主循环调用：调用 stm32_link_start() 后由 FreeRTOS 任务自动收发。
 * 怎么测试：烧录后日志应周期性显示 STM32 在线，发送 robot_tools 命令后 STM32 有动作。
 * 失败先检查：TX/RX 是否交叉、GND 是否共地、波特率是否 115200、STM32 是否烧录正式程序。
 */

/*
 * Clean communication note:
 * ESP32 UART1 GPIO47 TX -> STM32 PA10 RX, GPIO48 RX <- STM32 PA9 TX, 115200 8N1.
 * Protocol frame: AA 55 LEN CMD DATA... CHECKSUM, checksum is LEN+CMD+DATA low 8 bits.
 * In idf.py monitor, use "link" to see raw communication counters and "status" to see cached car state.
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    STM32_MOVE_FORWARD = 0,
    STM32_MOVE_BACKWARD,
    STM32_MOVE_TURN_LEFT,
    STM32_MOVE_TURN_RIGHT,
    STM32_MOVE_ROTATE_LEFT,
    STM32_MOVE_ROTATE_RIGHT,
} stm32_link_move_t;

typedef enum {
    STM32_EASING_LINEAR = 0,
    STM32_EASING_IN_OUT = 1,
    STM32_EASING_SMOOTH_STEP = 2,
} stm32_link_easing_t;

typedef struct {
    uint32_t tx_frames;
    uint32_t tx_failures;
    uint32_t rx_bytes;
    uint32_t rx_frames;
    uint32_t checksum_errors;
    uint32_t bad_length_errors;
    uint32_t unknown_frames;
    uint32_t heartbeat_acks;
    uint32_t status_basic_frames;
    uint32_t status_sensor_frames;
    uint32_t status_gps_frames;
    uint32_t status_gimbal_frames;
    uint32_t status_motor_frames;
    uint32_t status_behavior_frames;
    uint32_t last_tx_ms;
    uint32_t last_rx_ms;
    uint32_t last_valid_frame_ms;
    uint8_t last_tx_cmd;
    uint8_t last_rx_cmd;
    bool online;
} stm32_link_diag_t;

esp_err_t stm32_link_init(void);
esp_err_t stm32_link_start(void);
bool stm32_link_is_online(void);
void stm32_link_get_diagnostics(stm32_link_diag_t *out);

esp_err_t stm32_link_request_status(void);
esp_err_t stm32_link_stop(void);
esp_err_t stm32_link_move(stm32_link_move_t direction, uint8_t speed, uint16_t duration_ms);
esp_err_t stm32_link_set_speed(uint8_t speed);
esp_err_t stm32_link_gimbal_center(uint16_t duration_ms);
esp_err_t stm32_link_gimbal_set(uint8_t pan, uint8_t tilt, uint16_t duration_ms, stm32_link_easing_t easing);
esp_err_t stm32_link_gimbal_action(uint8_t action_id);
esp_err_t stm32_link_led_rgb(uint8_t r, uint8_t g, uint8_t b);
esp_err_t stm32_link_led_effect(uint8_t effect_id, uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms);
esp_err_t stm32_link_behavior_start(uint8_t behavior_id);
esp_err_t stm32_link_behavior_stop(void);
