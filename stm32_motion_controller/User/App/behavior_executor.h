#ifndef BEHAVIOR_EXECUTOR_H
#define BEHAVIOR_EXECUTOR_H

/*
 * 模块：behavior_executor
 * 负责什么：执行 STM32 本地能完成的高层行为，例如卖萌、休眠、唤醒。
 * 用到什么硬件：通过 car_control、gimbal_control、bsp_ws2812 间接控制电机、云台和灯光。
 * 初始化函数：Behavior_Init()。
 * 主循环调用：Behavior_Update(HAL_GetTick())，必须高频调用，不能阻塞。
 * 怎么测试：发送 CMD_BEHAVIOR_START，观察云台、灯光、电机按阶段动作。
 * 失败先检查：行为编号、急停/避障标志、云台和灯光底层闭环是否已经正常。
 */

#include "main.h"
#include <stdint.h>

#define BEHAVIOR_MANUAL_CONTROL 0u
#define BEHAVIOR_CUTE           1u
#define BEHAVIOR_SLEEP          2u
#define BEHAVIOR_WAKE_UP        3u

void Behavior_Init(void);
void Behavior_Start(uint8_t behavior_id);
void Behavior_Cancel(void);
void Behavior_Stop(void);
void Behavior_Update(uint32_t now_ms);
uint8_t Behavior_GetCurrent(void);
uint8_t Behavior_IsRunning(void);
uint8_t Behavior_GetStep(void);

#endif
