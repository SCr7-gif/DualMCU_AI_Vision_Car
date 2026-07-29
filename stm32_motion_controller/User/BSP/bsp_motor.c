#include "bsp_motor.h"
#include "tim.h"

/* 函数作用：把输入参数限制在本项目允许的安全范围内。 */
static uint8_t clamp_speed(uint8_t speed_percent)
{
  return (speed_percent > 100u) ? 100u : speed_percent;
}

/* 函数作用：把百分比速度换算成 TIM3 PWM 比较值。 */
static uint32_t speed_to_compare(uint8_t speed_percent)
{
  uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim3);
  return ((arr + 1u) * (uint32_t)clamp_speed(speed_percent)) / 100u;
}

/* 函数作用：设置本模块内部状态或输出目标。 */
static void set_left_dir(GPIO_PinState in1, GPIO_PinState in2)
{
  HAL_GPIO_WritePin(MOTOR_L_IN1_GPIO_Port, MOTOR_L_IN1_Pin, in1);
  HAL_GPIO_WritePin(MOTOR_L_IN2_GPIO_Port, MOTOR_L_IN2_Pin, in2);
}

/* 函数作用：设置本模块内部状态或输出目标。 */
static void set_right_dir(GPIO_PinState in1, GPIO_PinState in2)
{
  HAL_GPIO_WritePin(MOTOR_R_IN1_GPIO_Port, MOTOR_R_IN1_Pin, in1);
  HAL_GPIO_WritePin(MOTOR_R_IN2_GPIO_Port, MOTOR_R_IN2_Pin, in2);
}

/* 函数作用：设置本模块内部状态或输出目标。 */
static void set_pwm_percent(uint8_t left_speed, uint8_t right_speed)
{
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, speed_to_compare(left_speed));
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, speed_to_compare(right_speed));
}

/* 函数作用：初始化该 BSP 硬件模块需要的初始状态和外设输出。 */
void BspMotor_Init(void)
{
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
  BspMotor_StopNow();
}

/* 函数作用：关闭 TB6612 输出并把左右电机 PWM 置零。 */
void BspMotor_StopNow(void)
{
  set_pwm_percent(0u, 0u);
  set_left_dir(GPIO_PIN_RESET, GPIO_PIN_RESET);
  set_right_dir(GPIO_PIN_RESET, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(MOTOR_STBY_GPIO_Port, MOTOR_STBY_Pin, GPIO_PIN_RESET);
}

/* 函数作用：按方向和速度设置 TB6612 方向脚、STBY 和左右 PWM。 */
void BspMotor_Apply(BspMotor_Direction direction, uint8_t speed_percent)
{
  uint8_t speed = clamp_speed(speed_percent);
  uint8_t slow_speed = (uint8_t)((uint16_t)speed * 55u / 100u);

  if ((direction == BSP_MOTOR_DIR_STOP) || (speed == 0u))
  {
    BspMotor_StopNow();
    return;
  }

  HAL_GPIO_WritePin(MOTOR_STBY_GPIO_Port, MOTOR_STBY_Pin, GPIO_PIN_SET);

  switch (direction)
  {
    case BSP_MOTOR_DIR_FORWARD:
      set_left_dir(GPIO_PIN_SET, GPIO_PIN_RESET);
      set_right_dir(GPIO_PIN_SET, GPIO_PIN_RESET);
      set_pwm_percent(speed, speed);
      break;

    case BSP_MOTOR_DIR_BACKWARD:
      set_left_dir(GPIO_PIN_RESET, GPIO_PIN_SET);
      set_right_dir(GPIO_PIN_RESET, GPIO_PIN_SET);
      set_pwm_percent(speed, speed);
      break;

    case BSP_MOTOR_DIR_TURN_LEFT:
      set_left_dir(GPIO_PIN_SET, GPIO_PIN_RESET);
      set_right_dir(GPIO_PIN_SET, GPIO_PIN_RESET);
      set_pwm_percent(slow_speed, speed);
      break;

    case BSP_MOTOR_DIR_TURN_RIGHT:
      set_left_dir(GPIO_PIN_SET, GPIO_PIN_RESET);
      set_right_dir(GPIO_PIN_SET, GPIO_PIN_RESET);
      set_pwm_percent(speed, slow_speed);
      break;

    case BSP_MOTOR_DIR_ROTATE_LEFT:
      set_left_dir(GPIO_PIN_RESET, GPIO_PIN_SET);
      set_right_dir(GPIO_PIN_SET, GPIO_PIN_RESET);
      set_pwm_percent(speed, speed);
      break;

    case BSP_MOTOR_DIR_ROTATE_RIGHT:
      set_left_dir(GPIO_PIN_SET, GPIO_PIN_RESET);
      set_right_dir(GPIO_PIN_RESET, GPIO_PIN_SET);
      set_pwm_percent(speed, speed);
      break;

    case BSP_MOTOR_DIR_STOP:
    default:
      BspMotor_StopNow();
      break;
  }
}
