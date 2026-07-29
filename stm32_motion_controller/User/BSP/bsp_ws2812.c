#include "bsp_ws2812.h"
#include "tim.h"

#define WS2812_BITS_PER_LED 24u
#define WS2812_RESET_SLOTS 50u
#define WS2812_PWM_BUFFER_SIZE ((WS2812_LED_COUNT * WS2812_BITS_PER_LED) + WS2812_RESET_SLOTS)
#define WS2812_DUTY_0 29u
#define WS2812_DUTY_1 58u
#define WS2812_REFRESH_MS 50u

typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} Ws2812_Color;

static Ws2812_Color s_leds[WS2812_LED_COUNT];
static uint16_t s_pwm_buffer[WS2812_PWM_BUFFER_SIZE];
static volatile uint8_t s_dma_busy;
static uint8_t s_effect;
static uint8_t s_base_r;
static uint8_t s_base_g;
static uint8_t s_base_b;
static uint32_t s_effect_start_tick;
static uint32_t s_last_refresh_tick;
static uint16_t s_effect_duration_ms;

/* 函数作用：按 0~255 比例缩放一个 8 位颜色分量。 */
static uint8_t scale_u8(uint8_t value, uint8_t scale)
{
  return (uint8_t)(((uint16_t)value * scale) / 255u);
}

/* 函数作用：根据时间生成呼吸灯亮度比例。 */
static uint8_t breathe_scale(uint32_t elapsed_ms, uint16_t period_ms)
{
  uint16_t phase;

  if (period_ms == 0u)
  {
    return 255u;
  }

  phase = (uint16_t)(elapsed_ms % period_ms);
  if (phase < (period_ms / 2u))
  {
    return (uint8_t)((phase * 255u) / (period_ms / 2u));
  }

  return (uint8_t)(((period_ms - phase) * 255u) / (period_ms / 2u));
}

/* 函数作用：设置本模块内部状态或输出目标。 */
static void set_all(uint8_t r, uint8_t g, uint8_t b)
{
  uint8_t i;

  for (i = 0u; i < WS2812_LED_COUNT; i++)
  {
    s_leds[i].r = r;
    s_leds[i].g = g;
    s_leds[i].b = b;
  }
}

/* 函数作用：设置本模块内部状态或输出目标。 */
static void set_one(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
  if (index >= WS2812_LED_COUNT)
  {
    return;
  }

  s_leds[index].r = r;
  s_leds[index].g = g;
  s_leds[index].b = b;
}

/* 函数作用：把 WS2812 RGB 数据转换成 TIM1 PWM DMA 波形缓冲。 */
static void build_pwm_buffer(void)
{
  uint16_t pos = 0u;
  uint8_t led;

  for (led = 0u; led < WS2812_LED_COUNT; led++)
  {
    uint8_t bytes[3];
    uint8_t byte_index;

    bytes[0] = s_leds[led].g;
    bytes[1] = s_leds[led].r;
    bytes[2] = s_leds[led].b;

    /* 关键逻辑：WS2812 的颜色顺序是 GRB，不是常见 RGB。
     * 每个数据 bit 被转换成一个定时器 PWM 占空比，DMA 连续输出保证 800kHz 时序稳定。
     */
    for (byte_index = 0u; byte_index < 3u; byte_index++)
    {
      uint8_t bit;
      for (bit = 0u; bit < 8u; bit++)
      {
        if ((bytes[byte_index] & (uint8_t)(0x80u >> bit)) != 0u)
        {
          s_pwm_buffer[pos] = WS2812_DUTY_1;
        }
        else
        {
          s_pwm_buffer[pos] = WS2812_DUTY_0;
        }
        pos++;
      }
    }
  }

  while (pos < WS2812_PWM_BUFFER_SIZE)
  {
    s_pwm_buffer[pos] = 0u;
    pos++;
  }
}

/* 函数作用：组织并发送对应的数据、状态或控制消息。 */
static void send_now(void)
{
  if (s_dma_busy != 0u)
  {
    return;
  }

  build_pwm_buffer();
  s_dma_busy = 1u;
  HAL_TIM_PWM_Start_DMA(&htim1, TIM_CHANNEL_1, (uint32_t *)s_pwm_buffer, WS2812_PWM_BUFFER_SIZE);
}

/* 函数作用：返回指定灯效的默认红色分量。 */
static uint8_t default_r(uint8_t effect)
{
  switch (effect)
  {
    case WS2812_EFFECT_ERROR:
      return 80u;
    case WS2812_EFFECT_CUTE:
      return 80u;
    case WS2812_EFFECT_THINKING:
      return 70u;
    default:
      return 0u;
  }
}

/* 函数作用：返回指定灯效的默认绿色分量。 */
static uint8_t default_g(uint8_t effect)
{
  switch (effect)
  {
    case WS2812_EFFECT_LISTENING:
      return 70u;
    case WS2812_EFFECT_SPEAKING:
      return 50u;
    case WS2812_EFFECT_THINKING:
      return 45u;
    default:
      return 0u;
  }
}

/* 函数作用：返回指定灯效的默认蓝色分量。 */
static uint8_t default_b(uint8_t effect)
{
  switch (effect)
  {
    case WS2812_EFFECT_IDLE:
      return 30u;
    case WS2812_EFFECT_SPEAKING:
      return 70u;
    case WS2812_EFFECT_CUTE:
      return 60u;
    case WS2812_EFFECT_PATROL:
      return 80u;
    default:
      return 0u;
  }
}

/* 函数作用：按当前灯效和时间计算每颗 WS2812 的颜色。 */
static void apply_effect(uint32_t now_ms)
{
  uint32_t elapsed = now_ms - s_effect_start_tick;
  uint8_t scale;

  if ((s_effect_duration_ms > 0u) && (elapsed >= s_effect_duration_ms))
  {
    BspWs2812_SetEffect(WS2812_EFFECT_IDLE, 0u, 0u, 0u, 0u);
    return;
  }

  switch (s_effect)
  {
    case WS2812_EFFECT_SOLID:
      set_all(s_base_r, s_base_g, s_base_b);
      break;

    case WS2812_EFFECT_LISTENING:
      scale = breathe_scale(elapsed, 1200u);
      set_all(scale_u8(s_base_r, scale), scale_u8(s_base_g, scale), scale_u8(s_base_b, scale));
      break;

    case WS2812_EFFECT_SPEAKING:
      if (((elapsed / 120u) % 2u) == 0u)
      {
        set_all(s_base_r, s_base_g, s_base_b);
      }
      else
      {
        set_all(0u, 0u, 0u);
      }
      break;

    case WS2812_EFFECT_THINKING:
      scale = breathe_scale(elapsed, 1800u);
      set_all(scale_u8(s_base_r, scale), scale_u8(s_base_g, scale), scale_u8(s_base_b, scale));
      break;

    case WS2812_EFFECT_ERROR:
      if (((elapsed / 250u) % 2u) == 0u)
      {
        set_all(s_base_r, 0u, 0u);
      }
      else
      {
        set_all(0u, 0u, 0u);
      }
      break;

    case WS2812_EFFECT_CUTE:
    {
      uint8_t i;
      for (i = 0u; i < WS2812_LED_COUNT; i++)
      {
        if (((i + (uint8_t)(elapsed / 180u)) % 2u) == 0u)
        {
          set_one(i, s_base_r, 0u, s_base_b);
        }
        else
        {
          set_one(i, 0u, s_base_g, s_base_b);
        }
      }
      break;
    }

    case WS2812_EFFECT_PATROL:
    {
      uint8_t active = (uint8_t)((elapsed / 120u) % WS2812_LED_COUNT);
      set_all(0u, 0u, 6u);
      set_one(active, s_base_r, s_base_g, s_base_b);
      break;
    }

    case WS2812_EFFECT_IDLE:
    default:
      set_all(0u, 0u, s_base_b);
      break;
  }
}

/* 函数作用：初始化该 BSP 硬件模块需要的初始状态和外设输出。 */
void BspWs2812_Init(void)
{
  s_dma_busy = 0u;
  s_last_refresh_tick = 0u;
  BspWs2812_SetEffect(WS2812_EFFECT_IDLE, 0u, 0u, 0u, 0u);
  send_now();
}

/* 函数作用：周期刷新 WS2812 灯效，并在需要时发送新的 PWM DMA 数据。 */
void BspWs2812_Update(uint32_t now_ms)
{
  if ((now_ms - s_last_refresh_tick) < WS2812_REFRESH_MS)
  {
    return;
  }

  s_last_refresh_tick = now_ms;
  apply_effect(now_ms);
  send_now();
}

/* 函数作用：设置该 BSP 硬件模块的输出目标或工作效果。 */
void BspWs2812_SetRgb(uint8_t r, uint8_t g, uint8_t b)
{
  s_effect = WS2812_EFFECT_SOLID;
  s_base_r = r;
  s_base_g = g;
  s_base_b = b;
  s_effect_duration_ms = 0u;
  s_effect_start_tick = HAL_GetTick();
  set_all(r, g, b);
  send_now();
}

/* 函数作用：设置该 BSP 硬件模块的输出目标或工作效果。 */
void BspWs2812_SetEffect(uint8_t effect_id, uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms)
{
  if (effect_id > WS2812_EFFECT_SOLID)
  {
    effect_id = WS2812_EFFECT_ERROR;
  }

  s_effect = effect_id;
  s_base_r = (r != 0u) ? r : default_r(effect_id);
  s_base_g = (g != 0u) ? g : default_g(effect_id);
  s_base_b = (b != 0u) ? b : default_b(effect_id);
  s_effect_duration_ms = duration_ms;
  s_effect_start_tick = HAL_GetTick();
  apply_effect(s_effect_start_tick);
  send_now();
}

/* 函数作用：返回该 BSP 硬件模块最近一次缓存的数据。 */
uint8_t BspWs2812_GetEffect(void)
{
  return s_effect;
}

/* 函数作用：判断当前状态是否满足对应条件。 */
uint8_t BspWs2812_IsBusy(void)
{
  return s_dma_busy;
}

/* 函数作用：WS2812 PWM DMA 完成回调，停止本次灯带数据发送。 */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance != TIM1)
  {
    return;
  }

  HAL_TIM_PWM_Stop_DMA(&htim1, TIM_CHANNEL_1);
  s_dma_busy = 0u;
}
