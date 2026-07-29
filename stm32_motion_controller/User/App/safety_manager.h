#ifndef SAFETY_MANAGER_H
#define SAFETY_MANAGER_H

/*
 * 模块：safety_manager
 * 负责什么：STM32 本地最高安全保护，实现上电停车、STOP 停车、通信超时停车、超声波过近停车。
 * 用到什么硬件：TIM3_CH1/CH2 电机 PWM，TB6612 控制脚，HC-SR04 超声波距离。
 * 初始化函数：SafetyManager_Init()。
 * 主循环调用：SafetyManager_Update(HAL_GetTick())。
 * 怎么测试：上电后电机不转；3 秒无心跳后状态 flags 出现 timeout；障碍物小于 15cm 时自动停车。
 * 失败先检查：TIM3 是否初始化、TB6612 接线、HC-SR04 TRIG/ECHO 接线、ECHO 电平是否安全。
 */

#include "main.h"
#include <stdint.h>

#define SAFETY_COMM_TIMEOUT_MS 3000u
#define SAFETY_FLAG_MOTOR_STOPPED 0x01u
#define SAFETY_FLAG_COMM_TIMEOUT  0x02u
#define SAFETY_FLAG_OBSTACLE_NEAR 0x04u
#define SAFETY_FLAG_ULTRA_TIMEOUT 0x08u

void SafetyManager_Init(void);
void SafetyManager_NotifyValidCommand(uint32_t now_ms);
void SafetyManager_StopMotorNow(void);
void SafetyManager_Update(uint32_t now_ms);
uint8_t SafetyManager_GetFlags(void);
uint8_t SafetyManager_IsObstacleTooClose(void);
uint32_t SafetyManager_GetLastCommandTick(void);

#endif
