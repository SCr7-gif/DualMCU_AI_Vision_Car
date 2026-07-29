#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

/*
 * 模块：command_handler
 * 负责什么：处理 ESP32-P4 发来的高层命令，并上报基础状态、电机状态、云台状态、传感器状态、GPS 状态，执行 LED 状态灯命令。
 * 用到什么硬件：通过 safety_manager 间接控制电机停车；通过 car_protocol 回包。
 * 初始化函数：CommandHandler_Init()。
 * 主循环调用：无需直接调用；car_protocol 收到完整帧后调用 CommandHandler_HandleCommand()。
 * 怎么测试：发送 HEARTBEAT 收到 FF；发送 REQUEST_STATUS 收到 80/84/83/81/82；发送 LED_RGB 能看到 WS2812 变色。
 * 失败先检查：协议帧 checksum、USART1 DMA 接收、CarProtocol_SetCommandHandler 是否已设置。
 */

#include "main.h"
#include <stdint.h>

void CommandHandler_Init(void);
void CommandHandler_HandleCommand(uint8_t cmd, const uint8_t *data, uint8_t len);
void CommandHandler_SendBasicStatus(void);
void CommandHandler_SendMotorStatus(void);
void CommandHandler_SendSensorStatus(void);
void CommandHandler_SendGpsStatus(void);
void CommandHandler_SendGimbalStatus(void);
void CommandHandler_SendBehaviorStatus(void);

#endif
