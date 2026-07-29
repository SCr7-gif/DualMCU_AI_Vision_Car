#include "safety_manager.h"
#include "bsp_ultrasonic.h"
#include "car_control.h"

static uint32_t s_last_command_tick;
static uint8_t s_flags;

/* 函数作用：STM32 本地立即停车入口，并设置电机已停止标志。 */
void SafetyManager_StopMotorNow(void)
{
  Motor_StopEmergency();
  s_flags |= SAFETY_FLAG_MOTOR_STOPPED;
}

/* 函数作用：初始化本模块状态和所需硬件/系统资源。 */
void SafetyManager_Init(void)
{
  s_last_command_tick = HAL_GetTick();
  s_flags = SAFETY_FLAG_MOTOR_STOPPED;
  SafetyManager_StopMotorNow();
}

/* 函数作用：收到有效 ESP32 命令时刷新通信时间戳并清除超时标志。 */
void SafetyManager_NotifyValidCommand(uint32_t now_ms)
{
  s_last_command_tick = now_ms;
  s_flags &= (uint8_t)(~SAFETY_FLAG_COMM_TIMEOUT);
}

/* 函数作用：周期更新本模块状态，要求在主循环或任务中反复调用。 */
void SafetyManager_Update(uint32_t now_ms)
{
  Ultrasonic_Data ultra = BspUltrasonic_GetData();

  if ((now_ms - s_last_command_tick) > SAFETY_COMM_TIMEOUT_MS)
  {
    s_flags |= SAFETY_FLAG_COMM_TIMEOUT;
    SafetyManager_StopMotorNow();
  }

  if ((ultra.valid != 0u) && (ultra.distance_cm_x10 < ULTRASONIC_NEAR_STOP_CM_X10))
  {
    s_flags |= SAFETY_FLAG_OBSTACLE_NEAR;
    SafetyManager_StopMotorNow();
  }
  else
  {
    s_flags &= (uint8_t)(~SAFETY_FLAG_OBSTACLE_NEAR);
  }

  if (ultra.timeout != 0u)
  {
    s_flags |= SAFETY_FLAG_ULTRA_TIMEOUT;
  }
  else
  {
    s_flags &= (uint8_t)(~SAFETY_FLAG_ULTRA_TIMEOUT);
  }

  if (Motor_GetState() == MOTOR_STATE_STOP)
  {
    s_flags |= SAFETY_FLAG_MOTOR_STOPPED;
  }
  else
  {
    s_flags &= (uint8_t)(~SAFETY_FLAG_MOTOR_STOPPED);
  }
}

/* 函数作用：读取并返回本模块缓存的状态或配置值。 */
uint8_t SafetyManager_GetFlags(void)
{
  return s_flags;
}

/* 函数作用：判断当前状态是否满足对应条件。 */
uint8_t SafetyManager_IsObstacleTooClose(void)
{
  if ((s_flags & SAFETY_FLAG_OBSTACLE_NEAR) != 0u)
  {
    return 1u;
  }

  return BspUltrasonic_IsTooClose();
}

/* 函数作用：读取并返回本模块缓存的状态或配置值。 */
uint32_t SafetyManager_GetLastCommandTick(void)
{
  return s_last_command_tick;
}
