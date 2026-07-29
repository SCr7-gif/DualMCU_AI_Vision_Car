#ifndef BSP_MOTOR_H
#define BSP_MOTOR_H

/*
 * 模块：bsp_motor
 * 负责什么：直接控制 TB6612 电机驱动的 PWM、方向脚和 STBY。
 * 用到什么硬件：TIM3_CH1=PA6 左电机 PWM，TIM3_CH2=PA7 右电机 PWM；
 *               PB10=STBY，PB12/PB13=左电机方向，PB14/PB15=右电机方向。
 * 初始化函数：BspMotor_Init()。
 * 主循环调用：本模块不需要周期调用；上层 car_control 会按斜坡调用 BspMotor_Apply()。
 * 怎么测试：调用 BspMotor_StopNow() 后 PWM=0、STBY=0；调用 BspMotor_Apply() 后方向脚和 PWM 改变。
 * 失败先检查：TIM3 是否启动、TB6612 是否供电、STBY 是否接 PB10、左右 PWM/方向线是否接对。
 */

#include "main.h"
#include <stdint.h>

typedef enum
{
  BSP_MOTOR_DIR_STOP = 0,
  BSP_MOTOR_DIR_FORWARD,
  BSP_MOTOR_DIR_BACKWARD,
  BSP_MOTOR_DIR_TURN_LEFT,
  BSP_MOTOR_DIR_TURN_RIGHT,
  BSP_MOTOR_DIR_ROTATE_LEFT,
  BSP_MOTOR_DIR_ROTATE_RIGHT
} BspMotor_Direction;

void BspMotor_Init(void);
void BspMotor_StopNow(void);
void BspMotor_Apply(BspMotor_Direction direction, uint8_t speed_percent);

#endif
