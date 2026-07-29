#ifndef BSP_SERVO_H
#define BSP_SERVO_H

/*
 * 模块：bsp_servo
 * 负责什么：直接驱动两个 9g 舵机 PWM，只提供按角度设置脉宽的底层能力。
 * 用到什么硬件：TIM4_CH1=PB6 Pan 舵机，TIM4_CH2=PB7 Tilt 舵机，周期 20ms。
 * 初始化函数：BspServo_Init()。
 * 主循环调用：本模块不需要周期调用；上层 gimbal_control 会平滑计算角度后调用。
 * 怎么测试：初始化后 Pan/Tilt 都在 90 度；调用设置函数后 PWM 脉宽随角度变化。
 * 失败先检查：TIM4 是否初始化，PB6/PB7 是否接到舵机信号线，舵机供电是否足够并共地。
 */

#include "main.h"
#include <stdint.h>

#define SERVO_MIN_ANGLE 0u
#define SERVO_MAX_ANGLE 180u
#define SERVO_CENTER_ANGLE 90u
#define SERVO_MIN_PULSE_US 500u
#define SERVO_MAX_PULSE_US 2500u

void BspServo_Init(void);
void BspServo_SetPanAngle(uint8_t angle);
void BspServo_SetTiltAngle(uint8_t angle);

#endif
