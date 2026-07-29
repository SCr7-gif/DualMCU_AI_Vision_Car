#include "bsp_gps.h"
#include "usart.h"

static Gps_Data s_data;
static char s_rx_line[GPS_NMEA_MAX_LEN];
static uint8_t s_rx_pos;
static uint8_t s_collecting;

/* 函数作用：清除 GPS 串口错误标志，避免 ORE/FE 阻塞后续接收。 */
static void gps_clear_uart_error(void)
{
  volatile uint32_t sr;
  volatile uint32_t dr;

  if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE) != RESET)
  {
    sr = huart2.Instance->SR;
    dr = huart2.Instance->DR;
    (void)sr;
    (void)dr;
  }
}

/* 函数作用：判断当前状态是否满足对应条件。 */
static uint8_t gps_is_interesting_line(const char *line)
{
  if ((line[0] != '$') || (line[1] == '\0'))
  {
    return 0u;
  }

  return 1u;
}

/* 函数作用：判断当前状态是否满足对应条件。 */
static void gps_finish_line(uint32_t now_ms)
{
  uint8_t i;

  if (s_rx_pos == 0u)
  {
    return;
  }

  s_rx_line[s_rx_pos] = '\0';
  if (gps_is_interesting_line(s_rx_line) == 0u)
  {
    return;
  }

  for (i = 0u; i < GPS_NMEA_MAX_LEN; i++)
  {
    s_data.line[i] = s_rx_line[i];
    if (s_rx_line[i] == '\0')
    {
      break;
    }
  }

  s_data.valid = 1u;
  s_data.line_count++;
  s_data.last_update_tick = now_ms;
}

/* 函数作用：把 GPS 串口字节组装成一行 NMEA 文本。 */
static void gps_input_byte(uint8_t byte, uint32_t now_ms)
{
  if (byte == '$')
  {
    s_collecting = 1u;
    s_rx_pos = 0u;
    s_rx_line[s_rx_pos++] = (char)byte;
    return;
  }

  if (s_collecting == 0u)
  {
    return;
  }

  if (byte == '\r')
  {
    return;
  }

  if (byte == '\n')
  {
    gps_finish_line(now_ms);
    s_collecting = 0u;
    s_rx_pos = 0u;
    return;
  }

  if (s_rx_pos >= (GPS_NMEA_MAX_LEN - 1u))
  {
    s_data.overflow = 1u;
    s_collecting = 0u;
    s_rx_pos = 0u;
    return;
  }

  s_rx_line[s_rx_pos++] = (char)byte;
}

/* 函数作用：初始化该 BSP 硬件模块需要的初始状态和外设输出。 */
void BspGps_Init(void)
{
  uint8_t i;

  s_data.valid = 0u;
  s_data.overflow = 0u;
  s_data.line_count = 0u;
  s_data.last_update_tick = 0u;
  for (i = 0u; i < GPS_NMEA_MAX_LEN; i++)
  {
    s_data.line[i] = '\0';
    s_rx_line[i] = '\0';
  }
  s_rx_pos = 0u;
  s_collecting = 0u;
}

/* 函数作用：非阻塞周期更新该 BSP 硬件模块的采样或输出。 */
void BspGps_Update(uint32_t now_ms)
{
  uint8_t count = 0u;

  gps_clear_uart_error();

  while ((__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE) != RESET) && (count < GPS_POLL_MAX_BYTES))
  {
    uint8_t byte = (uint8_t)(huart2.Instance->DR & 0xFFu);
    gps_input_byte(byte, now_ms);
    count++;
  }
}

/* 函数作用：返回该 BSP 硬件模块最近一次缓存的数据。 */
Gps_Data BspGps_GetData(void)
{
  return s_data;
}
