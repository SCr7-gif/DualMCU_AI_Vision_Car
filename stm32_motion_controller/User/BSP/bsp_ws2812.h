#ifndef BSP_WS2812_H
#define BSP_WS2812_H

/*
 * 模块：bsp_ws2812
 * 负责什么：驱动 PA8 上的 WS2812 灯带，支持设置 RGB 和常用状态灯效。
 * 用到什么硬件：TIM1_CH1=PA8，DMA1_Channel2，TIM1 周期 1.25us。
 * 初始化函数：BspWs2812_Init()。
 * 主循环调用：BspWs2812_Update(HAL_GetTick())，按灯效周期刷新，不能阻塞。
 * 怎么测试：发送 CMD_LED_RGB 或 CMD_LED_EFFECT，观察灯带颜色、闪烁、呼吸是否变化。
 * 失败先检查：PA8 是否接 WS2812 DIN、5V 供电和 GND 是否共地、TIM1 DMA 是否加入工程、中断是否开启。
 */

#include "main.h"
#include <stdint.h>

#define WS2812_LED_COUNT 8u

typedef enum
{
  WS2812_EFFECT_IDLE = 0,
  WS2812_EFFECT_LISTENING = 1,
  WS2812_EFFECT_SPEAKING = 2,
  WS2812_EFFECT_THINKING = 3,
  WS2812_EFFECT_ERROR = 4,
  WS2812_EFFECT_CUTE = 5,
  WS2812_EFFECT_PATROL = 6,
  WS2812_EFFECT_SOLID = 7
} Ws2812_Effect;

void BspWs2812_Init(void);
void BspWs2812_Update(uint32_t now_ms);
void BspWs2812_SetRgb(uint8_t r, uint8_t g, uint8_t b);
void BspWs2812_SetEffect(uint8_t effect_id, uint8_t r, uint8_t g, uint8_t b, uint16_t duration_ms);
uint8_t BspWs2812_GetEffect(void);
uint8_t BspWs2812_IsBusy(void);

#endif
