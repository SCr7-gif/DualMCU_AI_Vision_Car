#include "app_main.h"

#include "bsp_dht11.h"
#include "bsp_gps.h"
#include "bsp_ultrasonic.h"
#include "bsp_ws2812.h"
#include "behavior_executor.h"
#include "car_control.h"
#include "car_protocol.h"
#include "command_handler.h"
#include "gimbal_control.h"
#include "safety_manager.h"
#include "usart.h"

#define APP_UART1_RX_DMA_SIZE 128u
#define APP_STATUS_REPORT_MS 1000u

static uint8_t s_uart1_rx_dma[APP_UART1_RX_DMA_SIZE];
static uint16_t s_uart1_rx_last_pos;
static uint32_t s_last_status_tick;

/* 函数作用：重启 USART1 DMA 环形接收，恢复 ESP32 控制链路。 */
static void App_Main_RestartUart1RxDma(void)
{
  HAL_UART_AbortReceive(&huart1);
  __HAL_UART_CLEAR_FEFLAG(&huart1);
  __HAL_UART_CLEAR_NEFLAG(&huart1);
  __HAL_UART_CLEAR_OREFLAG(&huart1);
  __HAL_UART_CLEAR_IDLEFLAG(&huart1);
  s_uart1_rx_last_pos = 0u;
  HAL_UART_Receive_DMA(&huart1, s_uart1_rx_dma, APP_UART1_RX_DMA_SIZE);
}

/* 函数作用：检测并清除 USART1 ORE/NE/FE 错误，必要时重启 DMA。 */
static void App_Main_RecoverUart1Errors(void)
{
  uint32_t error_flags = huart1.Instance->SR & (USART_SR_ORE | USART_SR_NE | USART_SR_FE);

  if (error_flags != 0u)
  {
    volatile uint32_t sr = huart1.Instance->SR;
    volatile uint32_t dr = huart1.Instance->DR;
    (void)sr;
    (void)dr;
    App_Main_RestartUart1RxDma();
  }
}

/* USART1 uses DMA circular RX. This poller extracts only the new bytes and
 * feeds them into car_protocol, so the parser can rebuild AA55 command frames.
 */
/* 函数作用：从 USART1 DMA 环形缓冲取出新增字节并交给协议解析器。 */
static void App_Main_PollUart1Dma(void)
{
  uint16_t pos;

  App_Main_RecoverUart1Errors();

  if (huart1.hdmarx == 0)
  {
    App_Main_RestartUart1RxDma();
    return;
  }

  pos = (uint16_t)(APP_UART1_RX_DMA_SIZE - __HAL_DMA_GET_COUNTER(huart1.hdmarx));

  if (pos == s_uart1_rx_last_pos)
  {
    return;
  }

  if (pos > s_uart1_rx_last_pos)
  {
    uint16_t i;
    for (i = s_uart1_rx_last_pos; i < pos; i++)
    {
      CarProtocol_InputByte(s_uart1_rx_dma[i]);
    }
  }
  else
  {
    uint16_t i;
    for (i = s_uart1_rx_last_pos; i < APP_UART1_RX_DMA_SIZE; i++)
    {
      CarProtocol_InputByte(s_uart1_rx_dma[i]);
    }
    for (i = 0u; i < pos; i++)
    {
      CarProtocol_InputByte(s_uart1_rx_dma[i]);
    }
  }

  s_uart1_rx_last_pos = pos;
}

/* 函数作用：STM32 应用层初始化入口，串起协议、执行器、传感器和安全模块。 */
void App_Main_Init(void)
{
  /* Communication chain:
   * ESP32 GPIO47 TX -> STM32 PA10 RX enters this DMA buffer.
   * CarProtocol parses the bytes and calls CommandHandler_HandleCommand().
   * Replies go back through USART1 TX on STM32 PA9 -> ESP32 GPIO48 RX.
   */
  CarProtocol_Init(&huart1);
  CarProtocol_SetCommandHandler(CommandHandler_HandleCommand);
  Motor_Init();
  Gimbal_Init();
  BspDht11_Init();
  BspGps_Init();
  BspUltrasonic_Init();
  BspWs2812_Init();
  Behavior_Init();
  CommandHandler_Init();
  SafetyManager_Init();

  s_uart1_rx_last_pos = 0u;
  s_last_status_tick = HAL_GetTick();
  App_Main_RestartUart1RxDma();

  CommandHandler_SendBasicStatus();
  CommandHandler_SendMotorStatus();
  CommandHandler_SendGimbalStatus();
  CommandHandler_SendBehaviorStatus();
}

/* 函数作用：STM32 应用层主循环入口，非阻塞轮询所有业务模块并周期上报状态。 */
void App_Main_Loop(void)
{
  uint32_t now = HAL_GetTick();

  /* 关键逻辑：所有业务模块都按非阻塞方式轮询。
   * 这里不能写长 delay，否则 UART 协议、超声波测距、云台插值和灯效都会卡顿。
   */
  App_Main_PollUart1Dma();
  BspDht11_Update(now);
  BspGps_Update(now);
  BspUltrasonic_Update(now);
  SafetyManager_Update(now);
  Behavior_Update(now);
  Motor_Update(now);
  Gimbal_Update(now);
  BspWs2812_Update(now);

  if ((now - s_last_status_tick) >= APP_STATUS_REPORT_MS)
  {
    /* Periodic status frames let ESP32 know STM32 is still alive even when no
     * user command is being sent from the serial monitor or Qwen tool calls.
     */
    s_last_status_tick = now;
    CommandHandler_SendBasicStatus();
    CommandHandler_SendMotorStatus();
    CommandHandler_SendGimbalStatus();
    CommandHandler_SendBehaviorStatus();
    CommandHandler_SendSensorStatus();
    CommandHandler_SendGpsStatus();
  }
}
