#include "bsp_ultrasonic.h"
#include "tim.h"

static Ultrasonic_Data s_data;
static uint8_t s_waiting_echo;
static uint32_t s_last_trigger_tick;

/* 函数作用：提供微秒级忙等待，用于单总线或传感器时序。 */
static void ultrasonic_delay_us(uint32_t us)
{
  uint32_t start;
  uint32_t ticks;

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  start = DWT->CYCCNT;
  ticks = us * (HAL_RCC_GetHCLKFreq() / 1000000u);

  while ((DWT->CYCCNT - start) < ticks)
  {
  }
}

/* 函数作用：向 HC-SR04 发送一次 TRIG 脉冲并进入等待 ECHO 状态。 */
static void ultrasonic_trigger_once(uint32_t now_ms)
{
  s_waiting_echo = 1u;
  s_last_trigger_tick = now_ms;

  __HAL_TIM_SET_COUNTER(&htim2, 0u);
  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC1 | TIM_FLAG_CC2 | TIM_FLAG_UPDATE);

  HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_RESET);
  ultrasonic_delay_us(2u);
  HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_SET);
  ultrasonic_delay_us(10u);
  HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_RESET);
}

/* 函数作用：初始化该 BSP 硬件模块需要的初始状态和外设输出。 */
void BspUltrasonic_Init(void)
{
  s_data.valid = 0u;
  s_data.timeout = 0u;
  s_data.distance_cm_x10 = ULTRASONIC_DISTANCE_INVALID;
  s_data.echo_us = 0u;
  s_data.last_update_tick = 0u;
  s_waiting_echo = 0u;
  s_last_trigger_tick = HAL_GetTick();

  HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_RESET);
  HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1);
  HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_2);
}

/* 函数作用：非阻塞周期更新该 BSP 硬件模块的采样或输出。 */
void BspUltrasonic_Update(uint32_t now_ms)
{
  if (s_waiting_echo != 0u)
  {
    if ((now_ms - s_last_trigger_tick) >= ULTRASONIC_ECHO_TIMEOUT_MS)
    {
      s_waiting_echo = 0u;
      s_data.valid = 0u;
      s_data.timeout = 1u;
      s_data.distance_cm_x10 = ULTRASONIC_DISTANCE_INVALID;
      s_data.echo_us = 0u;
      s_data.last_update_tick = now_ms;
    }
    return;
  }

  if ((now_ms - s_last_trigger_tick) >= ULTRASONIC_SAMPLE_INTERVAL_MS)
  {
    ultrasonic_trigger_once(now_ms);
  }
}

/* 函数作用：返回该 BSP 硬件模块最近一次缓存的数据。 */
Ultrasonic_Data BspUltrasonic_GetData(void)
{
  return s_data;
}

/* 函数作用：判断当前状态是否满足对应条件。 */
uint8_t BspUltrasonic_IsTooClose(void)
{
  if (s_data.valid == 0u)
  {
    return 0u;
  }

  return (s_data.distance_cm_x10 < ULTRASONIC_NEAR_STOP_CM_X10) ? 1u : 0u;
}

/* 函数作用：超声波 ECHO 输入捕获回调，计算高电平脉宽和距离。 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
  uint16_t echo_us;

  if (htim->Instance != TIM2)
  {
    return;
  }

  if (htim->Channel != HAL_TIM_ACTIVE_CHANNEL_2)
  {
    return;
  }

  echo_us = (uint16_t)HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
  s_waiting_echo = 0u;
  s_data.valid = 1u;
  s_data.timeout = 0u;
  s_data.echo_us = echo_us;
  s_data.distance_cm_x10 = (uint16_t)(((uint32_t)echo_us * 10u) / 58u);
  s_data.last_update_tick = HAL_GetTick();
}
