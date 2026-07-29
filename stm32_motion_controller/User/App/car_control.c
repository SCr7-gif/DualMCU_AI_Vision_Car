#include "car_control.h"

static Motor_State s_state;
static uint8_t s_current_speed;
static uint8_t s_target_speed;
static uint8_t s_ramp_start_speed;
static uint16_t s_ramp_ms;
static uint32_t s_ramp_start_tick;
static uint32_t s_command_end_tick;

/* 函数作用：把输入参数限制在本项目允许的安全范围内。 */
static uint8_t clamp_speed(uint8_t speed)
{
  return (speed > MOTOR_MAX_SPEED_PERCENT) ? MOTOR_MAX_SPEED_PERCENT : speed;
}

/* 函数作用：把输入参数限制在本项目允许的安全范围内。 */
static uint16_t clamp_duration(uint16_t duration_ms)
{
  if (duration_ms == 0u)
  {
    return 0u;
  }
  if (duration_ms < MOTOR_MIN_COMMAND_DURATION_MS)
  {
    return MOTOR_MIN_COMMAND_DURATION_MS;
  }
  return (duration_ms > MOTOR_MAX_COMMAND_DURATION_MS) ? MOTOR_MAX_COMMAND_DURATION_MS : duration_ms;
}

/* 函数作用：把应用层电机状态转换成 TB6612 底层方向枚举。 */
static BspMotor_Direction to_bsp_direction(Motor_State state)
{
  switch (state)
  {
    case MOTOR_STATE_FORWARD:
      return BSP_MOTOR_DIR_FORWARD;
    case MOTOR_STATE_BACKWARD:
      return BSP_MOTOR_DIR_BACKWARD;
    case MOTOR_STATE_TURN_LEFT:
      /* The motor channels are installed left/right reversed, so mirror only
       * steering directions here while keeping forward/backward unchanged.
       */
      return BSP_MOTOR_DIR_TURN_RIGHT;
    case MOTOR_STATE_TURN_RIGHT:
      return BSP_MOTOR_DIR_TURN_LEFT;
    case MOTOR_STATE_ROTATE_LEFT:
      return BSP_MOTOR_DIR_ROTATE_RIGHT;
    case MOTOR_STATE_ROTATE_RIGHT:
      return BSP_MOTOR_DIR_ROTATE_LEFT;
    case MOTOR_STATE_STOP:
    default:
      return BSP_MOTOR_DIR_STOP;
  }
}

/* 函数作用：按已过去时间在起始速度和目标速度之间线性插值。 */
static uint8_t lerp_speed(uint8_t start, uint8_t target, uint32_t elapsed, uint16_t duration)
{
  int16_t delta;
  int32_t value;

  if ((duration == 0u) || (elapsed >= duration))
  {
    return target;
  }

  delta = (int16_t)target - (int16_t)start;
  value = (int32_t)start + ((int32_t)delta * (int32_t)elapsed) / (int32_t)duration;
  if (value < 0)
  {
    value = 0;
  }
  if (value > 100)
  {
    value = 100;
  }
  return (uint8_t)value;
}

/* 函数作用：初始化本模块状态和所需硬件/系统资源。 */
void Motor_Init(void)
{
  BspMotor_Init();
  s_state = MOTOR_STATE_STOP;
  s_current_speed = 0u;
  s_target_speed = 0u;
  s_ramp_start_speed = 0u;
  s_ramp_ms = MOTOR_DEFAULT_RAMP_MS;
  s_ramp_start_tick = HAL_GetTick();
  s_command_end_tick = 0u;
  BspMotor_StopNow();
}

/* 函数作用：立即急停电机，清除速度斜坡和命令持续时间。 */
void Motor_StopEmergency(void)
{
  s_state = MOTOR_STATE_STOP;
  s_current_speed = 0u;
  s_target_speed = 0u;
  s_ramp_start_speed = 0u;
  s_ramp_ms = 0u;
  s_command_end_tick = 0u;
  BspMotor_StopNow();
}

/* 函数作用：设置新的目标速度和斜坡时间，让电机平滑变速。 */
void Motor_RampToSpeed(uint8_t target_speed, uint16_t ramp_ms)
{
  s_ramp_start_speed = s_current_speed;
  s_target_speed = clamp_speed(target_speed);
  s_ramp_ms = ramp_ms;
  s_ramp_start_tick = HAL_GetTick();
}

/* 函数作用：设置电机运动方向、目标速度和本次命令持续时间。 */
void Motor_Move(Motor_State state, uint8_t target_speed, uint16_t duration_ms)
{
  uint16_t safe_duration = clamp_duration(duration_ms);

  if ((state == MOTOR_STATE_STOP) || (target_speed == 0u))
  {
    Motor_StopEmergency();
    return;
  }

  s_state = state;
  s_command_end_tick = (safe_duration == 0u) ? 0u : (HAL_GetTick() + safe_duration);
  Motor_RampToSpeed(target_speed, MOTOR_DEFAULT_RAMP_MS);
}

/* 函数作用：周期更新本模块状态，要求在主循环或任务中反复调用。 */
void Motor_Update(uint32_t now_ms)
{
  if ((s_command_end_tick != 0u) && ((int32_t)(now_ms - s_command_end_tick) >= 0))
  {
    Motor_RampToSpeed(0u, MOTOR_STOP_RAMP_MS);
    s_command_end_tick = 0u;
  }

  s_current_speed = lerp_speed(s_ramp_start_speed,
                               s_target_speed,
                               now_ms - s_ramp_start_tick,
                               s_ramp_ms);

  if ((s_target_speed == 0u) && (s_current_speed == 0u))
  {
    s_state = MOTOR_STATE_STOP;
    BspMotor_StopNow();
  }
  else
  {
    BspMotor_Apply(to_bsp_direction(s_state), s_current_speed);
  }
}

/* 函数作用：读取并返回本模块缓存的状态或配置值。 */
Motor_State Motor_GetState(void)
{
  return s_state;
}

/* 函数作用：读取并返回本模块缓存的状态或配置值。 */
uint8_t Motor_GetTargetSpeed(void)
{
  return s_target_speed;
}

/* 函数作用：读取并返回本模块缓存的状态或配置值。 */
uint8_t Motor_GetCurrentSpeed(void)
{
  return s_current_speed;
}
