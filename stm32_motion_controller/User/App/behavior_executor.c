#include "behavior_executor.h"
#include "bsp_ws2812.h"
#include "car_control.h"
#include "gimbal_control.h"
#include "safety_manager.h"

typedef struct
{
  uint16_t start_ms;
  uint8_t gimbal_action;
  uint8_t led_effect;
} Behavior_CuteStep;

static const Behavior_CuteStep s_cute_steps[] =
{
  {0u,    GIMBAL_ACTION_CURIOUS_TILT, WS2812_EFFECT_CUTE},
  {1400u, GIMBAL_ACTION_NOD_YES,      WS2812_EFFECT_CUTE},
  {2600u, GIMBAL_ACTION_SHAKE_NO,     WS2812_EFFECT_CUTE},
  {3900u, GIMBAL_ACTION_CENTER,       WS2812_EFFECT_IDLE}
};

static uint8_t s_current_behavior;
static uint8_t s_running;
static uint8_t s_step;
static uint32_t s_start_tick;

/* 函数作用：判断当前状态是否满足对应条件。 */
static uint8_t is_valid_behavior(uint8_t behavior_id)
{
  return (behavior_id <= BEHAVIOR_WAKE_UP) ? 1u : 0u;
}

/* 函数作用：设置本模块内部状态或输出目标。 */
static void set_step(uint8_t step)
{
  s_step = step;
}

/* 函数作用：切换到手动行为状态，停止本地自动动作。 */
static void start_manual(void)
{
  s_current_behavior = BEHAVIOR_MANUAL_CONTROL;
  s_running = 0u;
  set_step(0u);
}

/* 函数作用：启动 STM32 本地卖萌行为的初始动作。 */
static void start_cute(uint32_t now_ms)
{
  s_current_behavior = BEHAVIOR_CUTE;
  s_running = 1u;
  s_start_tick = now_ms;
  set_step(0u);
  Motor_StopEmergency();
  BspWs2812_SetEffect(WS2812_EFFECT_CUTE, 80u, 20u, 80u, 0u);
  Gimbal_StartAction(GIMBAL_ACTION_CURIOUS_TILT);
}

/* 函数作用：启动 STM32 本地睡眠行为的初始动作。 */
static void start_sleep(uint32_t now_ms)
{
  s_current_behavior = BEHAVIOR_SLEEP;
  s_running = 1u;
  s_start_tick = now_ms;
  set_step(0u);
  Motor_StopEmergency();
  BspWs2812_SetEffect(WS2812_EFFECT_THINKING, 0u, 0u, 12u, 0u);
  Gimbal_SetPanTilt(GIMBAL_CENTER_PAN, 140u, 1500u, EASING_IN_OUT);
}

/* 函数作用：启动 STM32 本地唤醒行为的初始动作。 */
static void start_wake_up(uint32_t now_ms)
{
  s_current_behavior = BEHAVIOR_WAKE_UP;
  s_running = 1u;
  s_start_tick = now_ms;
  set_step(0u);
  Motor_StopEmergency();
  BspWs2812_SetEffect(WS2812_EFFECT_SPEAKING, 30u, 60u, 80u, 2200u);
  Gimbal_SetPanTilt(80u, GIMBAL_CENTER_TILT, 500u, EASING_IN_OUT);
}

/* 函数作用：初始化本模块状态和所需硬件/系统资源。 */
void Behavior_Init(void)
{
  s_current_behavior = BEHAVIOR_MANUAL_CONTROL;
  s_running = 0u;
  s_step = 0u;
  s_start_tick = HAL_GetTick();
}

/* 函数作用：启动本模块对应的任务、动作或工作流程。 */
void Behavior_Start(uint8_t behavior_id)
{
  uint32_t now = HAL_GetTick();

  if (is_valid_behavior(behavior_id) == 0u)
  {
    return;
  }

  switch (behavior_id)
  {
    case BEHAVIOR_MANUAL_CONTROL:
      start_manual();
      break;

    case BEHAVIOR_CUTE:
      start_cute(now);
      break;

    case BEHAVIOR_SLEEP:
      start_sleep(now);
      break;

    case BEHAVIOR_WAKE_UP:
      start_wake_up(now);
      break;

    default:
      break;
  }
}

/* 函数作用：取消当前本地行为，但保留手动控制状态。 */
void Behavior_Cancel(void)
{
  s_running = 0u;
  s_current_behavior = BEHAVIOR_MANUAL_CONTROL;
  set_step(0u);
}

/* 函数作用：停止本模块当前动作，并把状态切回安全/空闲。 */
void Behavior_Stop(void)
{
  Behavior_Cancel();
  Motor_StopEmergency();
  Gimbal_Stop();
  BspWs2812_SetEffect(WS2812_EFFECT_IDLE, 0u, 0u, 0u, 0u);
}

/* 函数作用：推进 STM32 本地行为状态机，按时间片触发电机、云台和灯效。 */
void Behavior_Update(uint32_t now_ms)
{
  uint32_t elapsed = now_ms - s_start_tick;

  if (s_running == 0u)
  {
    return;
  }

  if (SafetyManager_IsObstacleTooClose() != 0u)
  {
    Motor_StopEmergency();
  }

  if (s_current_behavior == BEHAVIOR_CUTE)
  {
    uint8_t next_step = s_step + 1u;
    if ((next_step < (uint8_t)(sizeof(s_cute_steps) / sizeof(s_cute_steps[0]))) &&
        (elapsed >= s_cute_steps[next_step].start_ms))
    {
      set_step(next_step);
      BspWs2812_SetEffect(s_cute_steps[s_step].led_effect, 80u, 20u, 80u, 0u);
      Gimbal_StartAction(s_cute_steps[s_step].gimbal_action);
    }

    if (elapsed >= 4700u)
    {
      s_running = 0u;
      s_current_behavior = BEHAVIOR_MANUAL_CONTROL;
      set_step(0u);
    }
    return;
  }

  if (s_current_behavior == BEHAVIOR_SLEEP)
  {
    if (elapsed >= 1600u)
    {
      s_running = 0u;
      set_step(1u);
    }
    return;
  }

  if (s_current_behavior == BEHAVIOR_WAKE_UP)
  {
    if ((s_step == 0u) && (elapsed >= 600u))
    {
      set_step(1u);
      Gimbal_SetPanTilt(100u, 88u, 500u, EASING_IN_OUT);
    }
    else if ((s_step == 1u) && (elapsed >= 1200u))
    {
      set_step(2u);
      Gimbal_Center(500u);
    }
    else if (elapsed >= 2200u)
    {
      s_running = 0u;
      s_current_behavior = BEHAVIOR_MANUAL_CONTROL;
      set_step(0u);
      BspWs2812_SetEffect(WS2812_EFFECT_IDLE, 0u, 0u, 0u, 0u);
    }
  }
}

/* 函数作用：读取并返回本模块缓存的状态或配置值。 */
uint8_t Behavior_GetCurrent(void)
{
  return s_current_behavior;
}

/* 函数作用：判断当前状态是否满足对应条件。 */
uint8_t Behavior_IsRunning(void)
{
  return s_running;
}

/* 函数作用：读取并返回本模块缓存的状态或配置值。 */
uint8_t Behavior_GetStep(void)
{
  return s_step;
}
