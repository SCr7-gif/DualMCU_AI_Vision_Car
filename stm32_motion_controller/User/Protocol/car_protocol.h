#ifndef CAR_PROTOCOL_H
#define CAR_PROTOCOL_H

/*
 * 模块：car_protocol
 * 负责什么：解析和发送 ESP32-P4 <-> STM32 的 AA 55 二进制帧。
 * 用到什么硬件：USART1，当前工程中 USART1 为 PA9/PA10，115200 8N1。
 * 初始化函数：CarProtocol_Init(&huart1)。
 * 主循环调用：App_Main_Loop() 从 USART1 DMA 缓冲取字节后调用 CarProtocol_InputByte()。
 * 怎么测试：ESP32 或 PC 发送 AA 55 LEN CMD DATA CHECKSUM，观察 STM32 是否返回 ACK/状态。
 * 失败先检查：串口交叉连接、GND 共地、波特率、LEN 和 CHECKSUM 是否正确。
 */

#include "main.h"
#include <stdint.h>

#define CAR_FRAME_HEAD0 0xAAu
#define CAR_FRAME_HEAD1 0x55u
#define CAR_PROTOCOL_MAX_DATA 64u

#define CMD_STOP              0x10u
#define CMD_MOVE_FORWARD      0x11u
#define CMD_MOVE_BACKWARD     0x12u
#define CMD_TURN_LEFT         0x13u
#define CMD_TURN_RIGHT        0x14u
#define CMD_ROTATE_LEFT       0x15u
#define CMD_ROTATE_RIGHT      0x16u
#define CMD_SET_SPEED         0x17u

#define CMD_GIMBAL_CENTER     0x20u
#define CMD_SET_PAN           0x21u
#define CMD_SET_TILT          0x22u
#define CMD_SET_PAN_TILT      0x23u
#define CMD_GIMBAL_ACTION     0x24u

#define CMD_LED_RGB           0x30u
#define CMD_LED_EFFECT        0x31u

#define CMD_REQUEST_STATUS    0x40u
#define CMD_BEHAVIOR_START    0x50u
#define CMD_BEHAVIOR_STOP     0x51u
#define CMD_HEARTBEAT         0x7Fu

#define CMD_STATUS_BASIC      0x80u
#define CMD_STATUS_SENSOR     0x81u
#define CMD_STATUS_GPS        0x82u
#define CMD_STATUS_GIMBAL     0x83u
#define CMD_STATUS_MOTOR      0x84u
#define CMD_STATUS_BEHAVIOR   0x85u
#define CMD_ERROR             0xE0u
#define CMD_HEARTBEAT_ACK     0xFFu

#define CAR_ERROR_CHECKSUM    0x01u
#define CAR_ERROR_UNKNOWN_CMD 0x02u
#define CAR_ERROR_BAD_LEN     0x03u
#define CAR_ERROR_BAD_PARAM   0x04u
#define CAR_ERROR_SAFETY      0x05u
#define CAR_ERROR_TIMEOUT     0x07u

typedef void (*CarProtocol_CommandHandler)(uint8_t cmd, const uint8_t *data, uint8_t len);

void CarProtocol_Init(UART_HandleTypeDef *uart);
void CarProtocol_SetCommandHandler(CarProtocol_CommandHandler handler);
void CarProtocol_InputByte(uint8_t byte);
HAL_StatusTypeDef CarProtocol_SendFrame(uint8_t cmd, const uint8_t *data, uint8_t len);
HAL_StatusTypeDef CarProtocol_SendError(uint8_t error_code, uint8_t detail);

#endif
