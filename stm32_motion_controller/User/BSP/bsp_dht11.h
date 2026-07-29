#ifndef BSP_DHT11_H
#define BSP_DHT11_H

/*
 * 模块：bsp_dht11
 * 作用：读取 DHT11 温湿度。
 * 硬件：PB5 = DHT11 DATA，单总线双向通信，空闲时由上拉保持高电平。
 * 时序：STM32 先把 DATA 拉低约 20 ms，再释放总线，随后读取 DHT11 回应的 40 bit。
 * 主循环：BspDht11_Update(HAL_GetTick()) 每 2 秒自动读取一次，避免超过 DHT11 采样频率。
 * 测试：上电等待 3 秒后请求传感器状态；dht_ok=1 时 temp/hum 有效。
 * 排查：如果 dht_ok=0，先看 PB5 DATA、电源、GND、上拉电阻和传感器方向。
 */

#include "main.h"
#include <stdint.h>

#define DHT11_MIN_INTERVAL_MS 2000u

typedef enum
{
  DHT11_ERROR_NONE = 0,
  DHT11_ERROR_WAIT_RESPONSE_LOW,
  DHT11_ERROR_WAIT_RESPONSE_HIGH,
  DHT11_ERROR_WAIT_RESPONSE_END,
  DHT11_ERROR_WAIT_BIT_HIGH,
  DHT11_ERROR_WAIT_BIT_LOW,
  DHT11_ERROR_CHECKSUM,
  DHT11_ERROR_ZERO_DATA
} Dht11_Error;

typedef struct
{
  uint8_t ok;
  uint8_t temperature;
  uint8_t humidity;
  Dht11_Error error;
  uint32_t last_read_tick;
} Dht11_Data;

void BspDht11_Init(void);
void BspDht11_Update(uint32_t now_ms);
Dht11_Data BspDht11_GetData(void);
uint8_t BspDht11_IsBusy(void);

#endif
