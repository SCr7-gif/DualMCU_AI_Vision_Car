#include "car_protocol.h"

typedef enum
{
  CAR_PROTO_WAIT_HEAD0 = 0,
  CAR_PROTO_WAIT_HEAD1,
  CAR_PROTO_WAIT_LEN,
  CAR_PROTO_WAIT_CMD,
  CAR_PROTO_WAIT_DATA,
  CAR_PROTO_WAIT_CHECKSUM
} CarProtocol_State;

static UART_HandleTypeDef *s_uart;
static CarProtocol_CommandHandler s_handler;
static CarProtocol_State s_state;
static uint8_t s_len;
static uint8_t s_cmd;
static uint8_t s_data[CAR_PROTOCOL_MAX_DATA];
static uint8_t s_data_pos;
static uint8_t s_checksum;

/* 函数作用：初始化本模块状态和所需硬件/系统资源。 */
void CarProtocol_Init(UART_HandleTypeDef *uart)
{
  s_uart = uart;
  s_handler = 0;
  s_state = CAR_PROTO_WAIT_HEAD0;
  s_len = 0;
  s_cmd = 0;
  s_data_pos = 0;
  s_checksum = 0;
}

/* 函数作用：处理对应事件、命令或请求，并把结果传给下游模块。 */
void CarProtocol_SetCommandHandler(CarProtocol_CommandHandler handler)
{
  s_handler = handler;
}

/* 函数作用：逐字节喂入 UART 数据，使用状态机重组 AA55 协议帧。 */
void CarProtocol_InputByte(uint8_t byte)
{
  /* 关键逻辑：AA55 协议按字节状态机解析。
   * 串口 DMA 每次吐出的数据长度不固定，所以不能假设一次读到完整帧。
   * 只有帧头、长度和 checksum 全部正确时，才把命令交给 command_handler.c。
   */
  switch (s_state)
  {
    case CAR_PROTO_WAIT_HEAD0:
      if (byte == CAR_FRAME_HEAD0)
      {
        s_state = CAR_PROTO_WAIT_HEAD1;
      }
      break;

    case CAR_PROTO_WAIT_HEAD1:
      s_state = (byte == CAR_FRAME_HEAD1) ? CAR_PROTO_WAIT_LEN : CAR_PROTO_WAIT_HEAD0;
      break;

    case CAR_PROTO_WAIT_LEN:
      s_len = byte;
      s_checksum = byte;
      s_data_pos = 0;
      if ((s_len == 0u) || (s_len > (CAR_PROTOCOL_MAX_DATA + 1u)))
      {
        s_state = CAR_PROTO_WAIT_HEAD0;
      }
      else
      {
        s_state = CAR_PROTO_WAIT_CMD;
      }
      break;

    case CAR_PROTO_WAIT_CMD:
      s_cmd = byte;
      s_checksum = (uint8_t)(s_checksum + byte);
      s_state = (s_len == 1u) ? CAR_PROTO_WAIT_CHECKSUM : CAR_PROTO_WAIT_DATA;
      break;

    case CAR_PROTO_WAIT_DATA:
      s_data[s_data_pos++] = byte;
      s_checksum = (uint8_t)(s_checksum + byte);
      if (s_data_pos >= (uint8_t)(s_len - 1u))
      {
        s_state = CAR_PROTO_WAIT_CHECKSUM;
      }
      break;

    case CAR_PROTO_WAIT_CHECKSUM:
      if ((byte == s_checksum) && (s_handler != 0))
      {
        s_handler(s_cmd, s_data, (uint8_t)(s_len - 1u));
      }
      s_state = CAR_PROTO_WAIT_HEAD0;
      break;

    default:
      s_state = CAR_PROTO_WAIT_HEAD0;
      break;
  }
}

/* 函数作用：按 AA55 协议格式打包并发送一帧数据。 */
HAL_StatusTypeDef CarProtocol_SendFrame(uint8_t cmd, const uint8_t *data, uint8_t len)
{
  static uint8_t frame[CAR_PROTOCOL_MAX_DATA + 5u];
  uint8_t frame_len;
  uint8_t checksum;
  uint8_t i;

  if ((s_uart == 0) || (len > CAR_PROTOCOL_MAX_DATA))
  {
    return HAL_ERROR;
  }

  /* LEN includes CMD plus DATA, and CHECKSUM is the low 8 bits of
   * LEN + CMD + every DATA byte. ESP32 uses the same rule in stm32_link.c.
   */
  frame_len = (uint8_t)(len + 1u);
  checksum = (uint8_t)(frame_len + cmd);

  frame[0] = CAR_FRAME_HEAD0;
  frame[1] = CAR_FRAME_HEAD1;
  frame[2] = frame_len;
  frame[3] = cmd;

  for (i = 0u; i < len; i++)
  {
    frame[4u + i] = (data != 0) ? data[i] : 0u;
    checksum = (uint8_t)(checksum + frame[4u + i]);
  }

  frame[4u + len] = checksum;
  return HAL_UART_Transmit(s_uart, frame, (uint16_t)(len + 5u), 20u);
}

/* 函数作用：组织并发送对应的数据、状态或控制消息。 */
HAL_StatusTypeDef CarProtocol_SendError(uint8_t error_code, uint8_t detail)
{
  uint8_t payload[2];
  payload[0] = error_code;
  payload[1] = detail;
  return CarProtocol_SendFrame(CMD_ERROR, payload, sizeof(payload));
}
