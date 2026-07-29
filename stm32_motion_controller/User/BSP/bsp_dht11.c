#include "bsp_dht11.h"

#define DHT11_START_LOW_MS 20u
#define DHT11_RELEASE_US 30u
#define DHT11_RESPONSE_TIMEOUT_US 120u
#define DHT11_BIT_TIMEOUT_US 120u
#define DHT11_ONE_HIGH_THRESHOLD_US 45u

typedef enum
{
  DHT11_STATE_IDLE = 0,
  DHT11_STATE_START_LOW
} Dht11_State;

static Dht11_State s_state;
static uint32_t s_state_tick;
static uint32_t s_last_start_tick;
static Dht11_Data s_data;

/* 函数作用：提供微秒级忙等待，用于单总线或传感器时序。 */
static void dht11_delay_us(uint32_t us)
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

/* 函数作用：设置本模块内部状态或输出目标。 */
static void dht11_set_output(void)
{
  GPIO_InitTypeDef init = {0};
  init.Pin = DHT11_Pin;
  init.Mode = GPIO_MODE_OUTPUT_OD;
  init.Pull = GPIO_PULLUP;
  init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(DHT11_GPIO_Port, &init);
}

/* 函数作用：设置本模块内部状态或输出目标。 */
static void dht11_set_input(void)
{
  GPIO_InitTypeDef init = {0};
  init.Pin = DHT11_Pin;
  init.Mode = GPIO_MODE_INPUT;
  init.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(DHT11_GPIO_Port, &init);
}

/* 函数作用：等待 DHT11 DATA 线进入指定电平，带微秒超时。 */
static uint8_t dht11_wait_level(GPIO_PinState level, uint16_t timeout_us)
{
  while (timeout_us > 0u)
  {
    if (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) == level)
    {
      return 1u;
    }
    dht11_delay_us(1u);
    timeout_us--;
  }
  return 0u;
}

/* 函数作用：等待 DHT11 DATA 线离开指定电平，带微秒超时。 */
static uint8_t dht11_wait_until_not_level(GPIO_PinState level, uint16_t timeout_us)
{
  while (timeout_us > 0u)
  {
    if (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) != level)
    {
      return 1u;
    }
    dht11_delay_us(1u);
    timeout_us--;
  }
  return 0u;
}

/* 函数作用：测量 DHT11 单个 bit 的高电平持续时间。 */
static uint8_t dht11_measure_high_us(uint16_t timeout_us, uint16_t *high_us)
{
  uint16_t count = 0u;

  while (HAL_GPIO_ReadPin(DHT11_GPIO_Port, DHT11_Pin) == GPIO_PIN_SET)
  {
    dht11_delay_us(1u);
    count++;
    if (count >= timeout_us)
    {
      *high_us = count;
      return 0u;
    }
  }

  *high_us = count;
  return 1u;
}

/* 函数作用：判断当前状态是否满足对应条件。 */
static void dht11_finish_with_error(Dht11_Error error)
{
  s_data.ok = 0u;
  s_data.error = error;
  s_data.last_read_tick = HAL_GetTick();
  dht11_set_output();
  /* Release the single-wire bus. DHT11 idles high through the pull-up. */
  HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET);
}

/* 函数作用：读取外设、缓存或协议字段中的数据。 */
static void dht11_read_bits(void)
{
  uint8_t raw[5] = {0u, 0u, 0u, 0u, 0u};
  uint8_t i;
  uint8_t bit;
  uint32_t primask;

  /* Host start is low for >=18 ms, then the MCU releases the bus instead of
   * driving it high. The sensor owns the line after the short release window.
   */
  HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET);
  dht11_delay_us(DHT11_RELEASE_US);
  dht11_set_input();

  /* 关键逻辑：DHT11 bit 宽只有几十微秒。
   * 读 40bit 的短窗口内先关中断，避免 SysTick/串口中断把高电平计时打断。
   */
  primask = __get_PRIMASK();
  __disable_irq();

  if (!dht11_wait_level(GPIO_PIN_RESET, DHT11_RESPONSE_TIMEOUT_US))
  {
    if (primask == 0u)
    {
      __enable_irq();
    }
    dht11_finish_with_error(DHT11_ERROR_WAIT_RESPONSE_LOW);
    return;
  }
  if (!dht11_wait_level(GPIO_PIN_SET, DHT11_RESPONSE_TIMEOUT_US))
  {
    if (primask == 0u)
    {
      __enable_irq();
    }
    dht11_finish_with_error(DHT11_ERROR_WAIT_RESPONSE_HIGH);
    return;
  }
  if (!dht11_wait_until_not_level(GPIO_PIN_SET, DHT11_RESPONSE_TIMEOUT_US))
  {
    if (primask == 0u)
    {
      __enable_irq();
    }
    dht11_finish_with_error(DHT11_ERROR_WAIT_RESPONSE_END);
    return;
  }

  for (i = 0u; i < 5u; i++)
  {
    for (bit = 0u; bit < 8u; bit++)
    {
      uint16_t high_us;

      if (!dht11_wait_level(GPIO_PIN_SET, DHT11_BIT_TIMEOUT_US))
      {
        if (primask == 0u)
        {
          __enable_irq();
        }
        dht11_finish_with_error(DHT11_ERROR_WAIT_BIT_HIGH);
        return;
      }

      if (!dht11_measure_high_us(DHT11_BIT_TIMEOUT_US, &high_us))
      {
        if (primask == 0u)
        {
          __enable_irq();
        }
        dht11_finish_with_error(DHT11_ERROR_WAIT_BIT_LOW);
        return;
      }

      raw[i] <<= 1;
      if (high_us >= DHT11_ONE_HIGH_THRESHOLD_US)
      {
        raw[i] |= 1u;
      }
    }
  }

  if (primask == 0u)
  {
    __enable_irq();
  }

  dht11_set_output();
  HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET);

  if (((uint8_t)(raw[0] + raw[1] + raw[2] + raw[3])) != raw[4])
  {
    dht11_finish_with_error(DHT11_ERROR_CHECKSUM);
    return;
  }

  if ((raw[0] == 0u) && (raw[1] == 0u) && (raw[2] == 0u) && (raw[3] == 0u) && (raw[4] == 0u))
  {
    dht11_finish_with_error(DHT11_ERROR_ZERO_DATA);
    return;
  }

  s_data.ok = 1u;
  s_data.humidity = raw[0];
  s_data.temperature = raw[2];
  s_data.error = DHT11_ERROR_NONE;
  s_data.last_read_tick = HAL_GetTick();
}

/* 函数作用：读取外设、缓存或协议字段中的数据。 */
static void dht11_start_read(uint32_t now_ms)
{
  dht11_set_output();
  /* DHT11 is not a normal input sensor: the STM32 must actively pull DATA low
   * first, then release the bus before the sensor sends its 40-bit reply.
   */
  HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_RESET);
  s_state = DHT11_STATE_START_LOW;
  s_state_tick = now_ms;
  s_last_start_tick = now_ms;
}

/* 函数作用：初始化该 BSP 硬件模块需要的初始状态和外设输出。 */
void BspDht11_Init(void)
{
  s_state = DHT11_STATE_IDLE;
  s_state_tick = 0u;
  s_last_start_tick = 0u;
  s_data.ok = 0u;
  s_data.temperature = 0u;
  s_data.humidity = 0u;
  s_data.error = DHT11_ERROR_NONE;
  s_data.last_read_tick = 0u;

  dht11_set_output();
  HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_SET);
}

/* 函数作用：按 DHT11 最小采样间隔触发温湿度读取并维护读取状态。 */
void BspDht11_Update(uint32_t now_ms)
{
  if (s_state == DHT11_STATE_IDLE)
  {
    if ((now_ms - s_last_start_tick) >= DHT11_MIN_INTERVAL_MS)
    {
      dht11_start_read(now_ms);
    }
    return;
  }

  if (s_state == DHT11_STATE_START_LOW)
  {
    if ((now_ms - s_state_tick) >= DHT11_START_LOW_MS)
    {
      dht11_read_bits();
      s_state = DHT11_STATE_IDLE;
    }
  }
}

/* 函数作用：返回该 BSP 硬件模块最近一次缓存的数据。 */
Dht11_Data BspDht11_GetData(void)
{
  return s_data;
}

/* 函数作用：判断当前状态是否满足对应条件。 */
uint8_t BspDht11_IsBusy(void)
{
  return (s_state != DHT11_STATE_IDLE) ? 1u : 0u;
}
