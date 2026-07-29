#include "easing.h"

/* 函数作用：根据缓动类型计算 0~1000 的归一化进度。 */
static uint16_t easing_scale(uint16_t elapsed_ms, uint16_t duration_ms, Easing_Type type)
{
  uint32_t t;
  uint32_t scale;

  if (duration_ms == 0u)
  {
    return 1000u;
  }

  if (elapsed_ms >= duration_ms)
  {
    return 1000u;
  }

  t = ((uint32_t)elapsed_ms * 1000u) / duration_ms;

  if (type == EASING_IN_OUT)
  {
    if (t < 500u)
    {
      scale = (2u * t * t) / 1000u;
    }
    else
    {
      uint32_t inv = 1000u - t;
      scale = 1000u - ((2u * inv * inv) / 1000u);
    }
  }
  else if (type == EASING_SMOOTH_STEP)
  {
    scale = (t * t * (3000u - 2u * t)) / 1000000u;
  }
  else
  {
    scale = t;
  }

  if (scale > 1000u)
  {
    scale = 1000u;
  }

  return (uint16_t)scale;
}

/* 函数作用：按缓动进度在两个 8 位数值之间插值，主要用于云台角度过渡。 */
uint8_t Easing_InterpolateU8(uint8_t start, uint8_t end, uint16_t elapsed_ms, uint16_t duration_ms, Easing_Type type)
{
  uint16_t scale = easing_scale(elapsed_ms, duration_ms, type);
  int16_t delta = (int16_t)end - (int16_t)start;
  int16_t value = (int16_t)start + (int16_t)(((int32_t)delta * scale) / 1000);

  if (value < 0)
  {
    value = 0;
  }
  if (value > 255)
  {
    value = 255;
  }

  return (uint8_t)value;
}
