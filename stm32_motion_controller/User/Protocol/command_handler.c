#include "command_handler.h"
#include "bsp_dht11.h"
#include "bsp_gps.h"
#include "bsp_ultrasonic.h"
#include "bsp_ws2812.h"
#include "behavior_executor.h"
#include "car_control.h"
#include "car_protocol.h"
#include "gimbal_control.h"
#include "safety_manager.h"

/* 函数作用：把 32 位毫秒运行时间压缩成协议中的低 16 位字段。 */
static uint16_t uptime_ms_to_u16(uint32_t now_ms)
{
  return (uint16_t)(now_ms & 0xFFFFu);
}

/* 函数作用：初始化本模块状态和所需硬件/系统资源。 */
void CommandHandler_Init(void)
{
  /* 当前闭环没有额外状态。后续电机、云台、传感器状态会在这里接入。 */
}

/* 函数作用：把 STM32 当前状态打包成对应状态帧回传给 ESP32。 */
void CommandHandler_SendBasicStatus(void)
{
  uint8_t payload[4];
  uint16_t uptime = uptime_ms_to_u16(HAL_GetTick());

  payload[0] = SafetyManager_GetFlags();
  payload[1] = (uint8_t)(uptime & 0xFFu);
  payload[2] = (uint8_t)((uptime >> 8) & 0xFFu);
  payload[3] = 0u;

  CarProtocol_SendFrame(CMD_STATUS_BASIC, payload, sizeof(payload));
}

/* 函数作用：把 STM32 当前状态打包成对应状态帧回传给 ESP32。 */
void CommandHandler_SendMotorStatus(void)
{
  uint8_t payload[3];
  payload[0] = (uint8_t)Motor_GetState();
  payload[1] = Motor_GetTargetSpeed();
  payload[2] = Motor_GetCurrentSpeed();
  CarProtocol_SendFrame(CMD_STATUS_MOTOR, payload, sizeof(payload));
}

/* 函数作用：把 STM32 当前状态打包成对应状态帧回传给 ESP32。 */
void CommandHandler_SendSensorStatus(void)
{
  uint8_t payload[5];
  Dht11_Data dht = BspDht11_GetData();
  Ultrasonic_Data ultra = BspUltrasonic_GetData();
  uint16_t distance = (ultra.valid != 0u) ? ultra.distance_cm_x10 : ULTRASONIC_DISTANCE_INVALID;

  payload[0] = (uint8_t)(distance & 0xFFu);
  payload[1] = (uint8_t)((distance >> 8) & 0xFFu);
  payload[2] = dht.temperature;
  payload[3] = dht.humidity;
  payload[4] = dht.ok;

  CarProtocol_SendFrame(CMD_STATUS_SENSOR, payload, sizeof(payload));
}

/* 函数作用：把 STM32 当前状态打包成对应状态帧回传给 ESP32。 */
void CommandHandler_SendGimbalStatus(void)
{
  uint8_t payload[4];

  payload[0] = Gimbal_GetPan();
  payload[1] = Gimbal_GetTilt();
  payload[2] = Gimbal_IsMoving();
  payload[3] = Gimbal_GetActionId();

  CarProtocol_SendFrame(CMD_STATUS_GIMBAL, payload, sizeof(payload));
}

/* 函数作用：把 STM32 当前状态打包成对应状态帧回传给 ESP32。 */
void CommandHandler_SendGpsStatus(void)
{
  uint8_t payload[CAR_PROTOCOL_MAX_DATA];
  uint8_t len = 0u;
  Gps_Data gps = BspGps_GetData();

  if (gps.valid != 0u)
  {
    while ((len < CAR_PROTOCOL_MAX_DATA) && (gps.line[len] != '\0'))
    {
      payload[len] = (uint8_t)gps.line[len];
      len++;
    }
  }

  CarProtocol_SendFrame(CMD_STATUS_GPS, payload, len);
}

/* 函数作用：把 STM32 当前状态打包成对应状态帧回传给 ESP32。 */
void CommandHandler_SendBehaviorStatus(void)
{
  uint8_t payload[3];

  payload[0] = Behavior_GetCurrent();
  payload[1] = Behavior_IsRunning();
  payload[2] = Behavior_GetStep();

  CarProtocol_SendFrame(CMD_STATUS_BEHAVIOR, payload, sizeof(payload));
}

/* 函数作用：读取外设、缓存或协议字段中的数据。 */
static uint16_t read_u16_le(const uint8_t *data)
{
  return (uint16_t)((uint16_t)data[1] | ((uint16_t)data[2] << 8));
}

/* 函数作用：读取外设、缓存或协议字段中的数据。 */
static uint16_t read_u16_pair(const uint8_t *data)
{
  return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/* 函数作用：读取外设、缓存或协议字段中的数据。 */
static Easing_Type read_easing(uint8_t value)
{
  if (value == (uint8_t)EASING_IN_OUT)
  {
    return EASING_IN_OUT;
  }
  if (value == (uint8_t)EASING_SMOOTH_STEP)
  {
    return EASING_SMOOTH_STEP;
  }
  return EASING_LINEAR;
}

/* 函数作用：校验命令参数并调用对应执行模块完成 STM32 动作。 */
static void handle_move_command(uint8_t cmd, const uint8_t *data, uint8_t len)
{
  Motor_State state;

  if (len != 3u)
  {
    CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
    return;
  }

  switch (cmd)
  {
    case CMD_MOVE_FORWARD:
      state = MOTOR_STATE_FORWARD;
      break;
    case CMD_MOVE_BACKWARD:
      state = MOTOR_STATE_BACKWARD;
      break;
    case CMD_TURN_LEFT:
      state = MOTOR_STATE_TURN_LEFT;
      break;
    case CMD_TURN_RIGHT:
      state = MOTOR_STATE_TURN_RIGHT;
      break;
    case CMD_ROTATE_LEFT:
      state = MOTOR_STATE_ROTATE_LEFT;
      break;
    case CMD_ROTATE_RIGHT:
      state = MOTOR_STATE_ROTATE_RIGHT;
      break;
    default:
      CarProtocol_SendError(CAR_ERROR_UNKNOWN_CMD, cmd);
      return;
  }

  if (SafetyManager_IsObstacleTooClose() != 0u)
  {
    CarProtocol_SendError(CAR_ERROR_SAFETY, cmd);
    CommandHandler_SendMotorStatus();
    return;
  }

  Behavior_Cancel();
  Motor_Move(state, data[0], read_u16_le(data));
  CommandHandler_SendMotorStatus();
}

/* 函数作用：校验命令参数并调用对应执行模块完成 STM32 动作。 */
static void handle_gimbal_command(uint8_t cmd, const uint8_t *data, uint8_t len)
{
  switch (cmd)
  {
    case CMD_GIMBAL_CENTER:
      if (len != 2u)
      {
        CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      }
      else
      {
        Behavior_Cancel();
        Gimbal_Center(read_u16_pair(&data[0]));
        CommandHandler_SendGimbalStatus();
      }
      break;

    case CMD_SET_PAN:
      if (len != 4u)
      {
        CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      }
      else
      {
        Behavior_Cancel();
        Gimbal_SetPan(data[0], read_u16_pair(&data[1]), read_easing(data[3]));
        CommandHandler_SendGimbalStatus();
      }
      break;

    case CMD_SET_TILT:
      if (len != 4u)
      {
        CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      }
      else
      {
        Behavior_Cancel();
        Gimbal_SetTilt(data[0], read_u16_pair(&data[1]), read_easing(data[3]));
        CommandHandler_SendGimbalStatus();
      }
      break;

    case CMD_SET_PAN_TILT:
      if (len != 5u)
      {
        CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      }
      else
      {
        Behavior_Cancel();
        Gimbal_SetPanTilt(data[0], data[1], read_u16_pair(&data[2]), read_easing(data[4]));
        CommandHandler_SendGimbalStatus();
      }
      break;

    case CMD_GIMBAL_ACTION:
      if (len != 1u)
      {
        CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      }
      else if (data[0] > GIMBAL_ACTION_MAX)
      {
        CarProtocol_SendError(CAR_ERROR_BAD_PARAM, cmd);
      }
      else
      {
        Behavior_Cancel();
        Gimbal_StartAction(data[0]);
        CommandHandler_SendGimbalStatus();
      }
      break;

    default:
      CarProtocol_SendError(CAR_ERROR_UNKNOWN_CMD, cmd);
      break;
  }
}

/* 函数作用：校验命令参数并调用对应执行模块完成 STM32 动作。 */
static void handle_led_command(uint8_t cmd, const uint8_t *data, uint8_t len)
{
  if (cmd == CMD_LED_RGB)
  {
    if (len != 3u)
    {
      CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      return;
    }

    Behavior_Cancel();
    BspWs2812_SetRgb(data[0], data[1], data[2]);
    CommandHandler_SendBasicStatus();
    return;
  }

  if (cmd == CMD_LED_EFFECT)
  {
    if (len != 6u)
    {
      CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      return;
    }
    if (data[0] > (uint8_t)WS2812_EFFECT_SOLID)
    {
      CarProtocol_SendError(CAR_ERROR_BAD_PARAM, cmd);
      return;
    }

    Behavior_Cancel();
    BspWs2812_SetEffect(data[0], data[1], data[2], data[3], read_u16_pair(&data[4]));
    CommandHandler_SendBasicStatus();
    return;
  }

  CarProtocol_SendError(CAR_ERROR_UNKNOWN_CMD, cmd);
}

/* 函数作用：校验命令参数并调用对应执行模块完成 STM32 动作。 */
static void handle_behavior_command(uint8_t cmd, const uint8_t *data, uint8_t len)
{
  if (cmd == CMD_BEHAVIOR_START)
  {
    if (len != 1u)
    {
      CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      return;
    }
    if (data[0] > BEHAVIOR_WAKE_UP)
    {
      CarProtocol_SendError(CAR_ERROR_BAD_PARAM, cmd);
      return;
    }

    Behavior_Start(data[0]);
    CommandHandler_SendBehaviorStatus();
    CommandHandler_SendMotorStatus();
    CommandHandler_SendGimbalStatus();
    return;
  }

  if (cmd == CMD_BEHAVIOR_STOP)
  {
    if (len != 0u)
    {
      CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      return;
    }

    Behavior_Stop();
    CommandHandler_SendBehaviorStatus();
    CommandHandler_SendMotorStatus();
    CommandHandler_SendGimbalStatus();
    return;
  }

  CarProtocol_SendError(CAR_ERROR_UNKNOWN_CMD, cmd);
}

/* 函数作用：STM32 命令总分发入口，把协议命令派发到电机、云台、灯光、行为等模块。 */
void CommandHandler_HandleCommand(uint8_t cmd, const uint8_t *data, uint8_t len)
{
  uint8_t seq;

  /* This is the command dispatcher called by car_protocol after a valid frame.
   * It executes the real car action and immediately returns ACK/status frames
   * so ESP32 can update the monitor, web debug page, and Qwen tool state.
   */
  SafetyManager_NotifyValidCommand(HAL_GetTick());

  switch (cmd)
  {
    case CMD_HEARTBEAT:
      seq = (len >= 1u) ? data[0] : 0u;
      CarProtocol_SendFrame(CMD_HEARTBEAT_ACK, &seq, 1u);
      break;

    case CMD_REQUEST_STATUS:
      if (len != 0u)
      {
        CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      }
      else
      {
        CommandHandler_SendBasicStatus();
        CommandHandler_SendMotorStatus();
        CommandHandler_SendGimbalStatus();
        CommandHandler_SendBehaviorStatus();
        CommandHandler_SendSensorStatus();
        CommandHandler_SendGpsStatus();
      }
      break;

    case CMD_STOP:
      if (len != 0u)
      {
        CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      }
      Behavior_Stop();
      SafetyManager_StopMotorNow();
      Gimbal_Stop();
      CommandHandler_SendMotorStatus();
      CommandHandler_SendGimbalStatus();
      CommandHandler_SendBehaviorStatus();
      break;

    case CMD_MOVE_FORWARD:
    case CMD_MOVE_BACKWARD:
    case CMD_TURN_LEFT:
    case CMD_TURN_RIGHT:
    case CMD_ROTATE_LEFT:
    case CMD_ROTATE_RIGHT:
      handle_move_command(cmd, data, len);
      break;

    case CMD_GIMBAL_CENTER:
    case CMD_SET_PAN:
    case CMD_SET_TILT:
    case CMD_SET_PAN_TILT:
    case CMD_GIMBAL_ACTION:
      handle_gimbal_command(cmd, data, len);
      break;

    case CMD_LED_RGB:
    case CMD_LED_EFFECT:
      handle_led_command(cmd, data, len);
      break;

    case CMD_BEHAVIOR_START:
    case CMD_BEHAVIOR_STOP:
      handle_behavior_command(cmd, data, len);
      break;

    case CMD_SET_SPEED:
      if (len != 1u)
      {
        CarProtocol_SendError(CAR_ERROR_BAD_LEN, cmd);
      }
      else if ((Motor_GetState() == MOTOR_STATE_STOP) && (data[0] > 0u))
      {
        CarProtocol_SendError(CAR_ERROR_BAD_PARAM, cmd);
      }
      else if ((SafetyManager_IsObstacleTooClose() != 0u) && (data[0] > 0u))
      {
        CarProtocol_SendError(CAR_ERROR_SAFETY, cmd);
      }
      else
      {
        Behavior_Cancel();
        Motor_RampToSpeed(data[0], MOTOR_DEFAULT_RAMP_MS);
        CommandHandler_SendMotorStatus();
      }
      break;

    default:
      CarProtocol_SendError(CAR_ERROR_UNKNOWN_CMD, cmd);
      break;
  }
}
