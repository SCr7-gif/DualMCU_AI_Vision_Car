#include "bsp_servo.h"
#include "tim.h"

/* 函数作用：把舵机角度换算成 500~2500us 的 PWM 脉宽。 */
static uint16_t servo_angle_to_pulse(uint8_t angle)
{
  if (angle > SERVO_MAX_ANGLE)
  {
    angle = SERVO_MAX_ANGLE;
  }

  return (uint16_t)(SERVO_MIN_PULSE_US +
                    (((uint32_t)(SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle) / SERVO_MAX_ANGLE));
}

/* 函数作用：反转舵机角度，用于匹配实际安装方向。 */
static uint8_t invert_servo_angle(uint8_t angle)
{
  if (angle > SERVO_MAX_ANGLE)
  {
    angle = SERVO_MAX_ANGLE;
  }
  return (uint8_t)(SERVO_MAX_ANGLE - angle);
}

/* 函数作用：初始化该 BSP 硬件模块需要的初始状态和外设输出。 */
void BspServo_Init(void)
{
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
  BspServo_SetPanAngle(SERVO_CENTER_ANGLE);
  BspServo_SetTiltAngle(SERVO_CENTER_ANGLE);
}

/* 函数作用：设置该 BSP 硬件模块的输出目标或工作效果。 */
void BspServo_SetPanAngle(uint8_t angle)
{
  /* The gimbal is mounted reversed on this car, so logical pan must be
   * mirrored before it reaches the physical PWM pulse.
   */
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, servo_angle_to_pulse(invert_servo_angle(angle)));
}

/* 函数作用：设置该 BSP 硬件模块的输出目标或工作效果。 */
void BspServo_SetTiltAngle(uint8_t angle)
{
  /* Keep the rest of the firmware using normal logical angles:
   * 90 is center, larger/smaller values mean the same thing to ESP32/AI.
   */
  __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, servo_angle_to_pulse(invert_servo_angle(angle)));
}
