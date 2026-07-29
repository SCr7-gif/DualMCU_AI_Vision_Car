#ifndef BSP_GPS_H
#define BSP_GPS_H

/*
 * 模块：bsp_gps
 * 负责什么：从 USART2 非阻塞接收 GPS NMEA 文本，缓存最新一行原始 NMEA。
 * 用到什么硬件：USART2，PA2=TX，PA3=RX，默认 9600 8N1。
 * 初始化函数：BspGps_Init()。
 * 主循环调用：BspGps_Update(HAL_GetTick())，轮询 RXNE，每次最多处理少量字节，不能阻塞。
 * 怎么测试：GPS TX 接 STM32 PA3 后，请求 STATUS_GPS，能看到 $GPRMC/$GPGGA/$GNRMC/$GNGGA 等 NMEA 文本。
 * 失败先检查：GPS TX/RX 是否交叉、GND 是否共地、波特率是否 9600、天线是否能看到天空、是否收到完整换行。
 */

#include "main.h"
#include <stdint.h>

#define GPS_NMEA_MAX_LEN 83u
#define GPS_POLL_MAX_BYTES 32u

typedef struct
{
  uint8_t valid;
  uint8_t overflow;
  uint16_t line_count;
  uint32_t last_update_tick;
  char line[GPS_NMEA_MAX_LEN];
} Gps_Data;

void BspGps_Init(void);
void BspGps_Update(uint32_t now_ms);
Gps_Data BspGps_GetData(void);

#endif
