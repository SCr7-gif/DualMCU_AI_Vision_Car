#ifndef CAR_CONTROL_H
#define CAR_CONTROL_H

/*
 * 模块：car_control
 * 负责什么：电机高层控制，管理方向、目标速度、当前速度、速度斜坡和运动持续时间。
 * 用到什么硬件：通过 bsp_motor 间接控制 TB6612 和 TIM3 PWM，不直接暴露 PWM 给协议层或 AI。
 * 初始化函数：Motor_Init()。
 * 主循环调用：Motor_Update(HAL_GetTick())，必须高频调用，不能阻塞。
 * 怎么测试：发送低速运动命令，观察 current_speed 从 0 平滑上升；STOP 立即归零。
 * 失败先检查：BspMotor_Init() 是否调用、TIM3 PWM 是否启动、命令 payload 长度是否为 speed + duration。
 */

#include "bsp_motor.h"
#include <stdint.h>

#define MOTOR_MAX_SPEED_PERCENT 60u
#define MOTOR_DEFAULT_RAMP_MS 180u
#define MOTOR_STOP_RAMP_MS 200u
#define MOTOR_MIN_COMMAND_DURATION_MS 1200u
#define MOTOR_MAX_COMMAND_DURATION_MS 5000u

typedef enum
{
  MOTOR_STATE_STOP = 0,
  MOTOR_STATE_FORWARD,
  MOTOR_STATE_BACKWARD,
  MOTOR_STATE_TURN_LEFT,
  MOTOR_STATE_TURN_RIGHT,
  MOTOR_STATE_ROTATE_LEFT,
  MOTOR_STATE_ROTATE_RIGHT
} Motor_State;

void Motor_Init(void);
void Motor_StopEmergency(void);
void Motor_Move(Motor_State state, uint8_t target_speed, uint16_t duration_ms);
void Motor_RampToSpeed(uint8_t target_speed, uint16_t ramp_ms);
void Motor_Update(uint32_t now_ms);
Motor_State Motor_GetState(void);
uint8_t Motor_GetTargetSpeed(void);
uint8_t Motor_GetCurrentSpeed(void);

#endif
