#ifndef GIMBAL_CONTROL_H
#define GIMBAL_CONTROL_H

/*
 * 模块：gimbal_control
 * 负责什么：云台运动引擎，管理 Pan/Tilt 当前角度、关键帧队列、缓动、动作中断和状态上报。
 * 用到什么硬件：通过 bsp_servo 使用 TIM4_CH1=PB6 Pan、TIM4_CH2=PB7 Tilt。
 * 初始化函数：Gimbal_Init()。
 * 主循环调用：Gimbal_Update(HAL_GetTick())，必须高频调用，不能阻塞。
 * 怎么测试：发送云台回中、设置角度、动作序列命令，观察舵机平滑移动且 Tilt 始终在 30 到 150 度。
 * 失败先检查：TIM4 PWM 是否启动，PB6/PB7 接线和供电，命令 payload 长度，Tilt 是否被硬限制。
 */

#include "easing.h"
#include "main.h"
#include <stdint.h>

#define GIMBAL_PAN_MIN 0u
#define GIMBAL_PAN_MAX 180u
#define GIMBAL_TILT_MIN 30u
#define GIMBAL_TILT_MAX 150u
#define GIMBAL_CENTER_PAN 90u
#define GIMBAL_CENTER_TILT 90u
#define GIMBAL_DEFAULT_DURATION_MS 600u
#define GIMBAL_MAX_KEYFRAMES 8u

#define GIMBAL_ACTION_CENTER 0u
#define GIMBAL_ACTION_LOOK_LEFT_SOFT 1u
#define GIMBAL_ACTION_LOOK_RIGHT_SOFT 2u
#define GIMBAL_ACTION_NOD_YES 3u
#define GIMBAL_ACTION_SHAKE_NO 4u
#define GIMBAL_ACTION_SCAN_SLOW 5u
#define GIMBAL_ACTION_CURIOUS_TILT 6u
#define GIMBAL_ACTION_LOOK_UP_SOFT 7u
#define GIMBAL_ACTION_LOOK_DOWN_SOFT 8u
#define GIMBAL_ACTION_SCAN_FAST 9u
#define GIMBAL_ACTION_SLEEPY 10u
#define GIMBAL_ACTION_WAKE_UP 11u
#define GIMBAL_ACTION_SHY_LOOK_DOWN 12u
#define GIMBAL_ACTION_HAPPY_WIGGLE 13u
#define GIMBAL_ACTION_THINKING_MOTION 14u
#define GIMBAL_ACTION_MAX GIMBAL_ACTION_THINKING_MOTION

typedef struct
{
  uint8_t pan;
  uint8_t tilt;
  uint16_t duration_ms;
  uint16_t hold_ms;
  Easing_Type easing;
} Gimbal_Keyframe;

void Gimbal_Init(void);
void Gimbal_Update(uint32_t now_ms);
void Gimbal_Stop(void);
void Gimbal_Center(uint16_t duration_ms);
void Gimbal_SetPan(uint8_t pan, uint16_t duration_ms, Easing_Type easing);
void Gimbal_SetTilt(uint8_t tilt, uint16_t duration_ms, Easing_Type easing);
void Gimbal_SetPanTilt(uint8_t pan, uint8_t tilt, uint16_t duration_ms, Easing_Type easing);
uint8_t Gimbal_StartAction(uint8_t action_id);
uint8_t Gimbal_GetPan(void);
uint8_t Gimbal_GetTilt(void);
uint8_t Gimbal_IsMoving(void);
uint8_t Gimbal_GetActionId(void);

#endif
