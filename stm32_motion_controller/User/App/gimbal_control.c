#include "gimbal_control.h"
#include "bsp_servo.h"

static Gimbal_Keyframe s_queue[GIMBAL_MAX_KEYFRAMES];
static uint8_t s_queue_count;
static uint8_t s_queue_index;
static uint8_t s_current_pan;
static uint8_t s_current_tilt;
static uint8_t s_start_pan;
static uint8_t s_start_tilt;
static uint8_t s_moving;
static uint8_t s_holding;
static uint8_t s_action_id;
static uint32_t s_segment_start_tick;
static uint32_t s_hold_start_tick;

/* 函数作用：把输入参数限制在本项目允许的安全范围内。 */
static uint8_t clamp_pan(uint8_t pan)
{
  return (pan > GIMBAL_PAN_MAX) ? GIMBAL_PAN_MAX : pan;
}

/* 函数作用：把输入参数限制在本项目允许的安全范围内。 */
static uint8_t clamp_tilt(uint8_t tilt)
{
  if (tilt < GIMBAL_TILT_MIN)
  {
    return GIMBAL_TILT_MIN;
  }
  if (tilt > GIMBAL_TILT_MAX)
  {
    return GIMBAL_TILT_MAX;
  }
  return tilt;
}

/* 函数作用：设置本模块内部状态或输出目标。 */
static uint8_t offset_u8(uint8_t value, int16_t offset, uint8_t min_value, uint8_t max_value)
{
  int16_t result = (int16_t)value + offset;
  if (result < (int16_t)min_value)
  {
    result = (int16_t)min_value;
  }
  if (result > (int16_t)max_value)
  {
    result = (int16_t)max_value;
  }
  return (uint8_t)result;
}

/* 函数作用：把云台动作持续时间规范到默认值或允许范围。 */
static uint16_t normalize_duration(uint16_t duration_ms)
{
  if (duration_ms == 0u)
  {
    return GIMBAL_DEFAULT_DURATION_MS;
  }
  return duration_ms;
}

/* 函数作用：把当前 Pan/Tilt 角度真正写入底层舵机 PWM。 */
static void apply_current_angle(void)
{
  BspServo_SetPanAngle(s_current_pan);
  BspServo_SetTiltAngle(s_current_tilt);
}

/* 函数作用：启动云台关键帧队列中的当前段运动。 */
static void start_segment(uint32_t now_ms)
{
  s_start_pan = s_current_pan;
  s_start_tilt = s_current_tilt;
  s_segment_start_tick = now_ms;
  s_holding = 0u;
}

/* 函数作用：清空云台关键帧队列和运动状态。 */
static void clear_queue(void)
{
  s_queue_count = 0u;
  s_queue_index = 0u;
  s_moving = 0u;
  s_holding = 0u;
  s_action_id = GIMBAL_ACTION_CENTER;
}

/* 函数作用：向云台动作队列追加一个目标角度、持续时间和保持时间。 */
static void push_keyframe(uint8_t pan, uint8_t tilt, uint16_t duration_ms, uint16_t hold_ms, Easing_Type easing)
{
  if (s_queue_count >= GIMBAL_MAX_KEYFRAMES)
  {
    return;
  }

  s_queue[s_queue_count].pan = clamp_pan(pan);
  s_queue[s_queue_count].tilt = clamp_tilt(tilt);
  s_queue[s_queue_count].duration_ms = normalize_duration(duration_ms);
  s_queue[s_queue_count].hold_ms = hold_ms;
  s_queue[s_queue_count].easing = easing;
  s_queue_count++;
}

/* 函数作用：从队列首段开始执行云台预设动作。 */
static void start_queue(uint8_t action_id)
{
  if (s_queue_count == 0u)
  {
    return;
  }

  s_queue_index = 0u;
  s_action_id = action_id;
  s_moving = 1u;
  start_segment(HAL_GetTick());
}

/* 函数作用：当前云台关键帧结束后切换到下一段或结束动作。 */
static void advance_keyframe(uint32_t now_ms)
{
  s_queue_index++;
  if (s_queue_index >= s_queue_count)
  {
    s_moving = 0u;
    s_holding = 0u;
    return;
  }

  start_segment(now_ms);
}

/* 函数作用：初始化本模块状态和所需硬件/系统资源。 */
void Gimbal_Init(void)
{
  BspServo_Init();
  s_current_pan = GIMBAL_CENTER_PAN;
  s_current_tilt = GIMBAL_CENTER_TILT;
  s_start_pan = s_current_pan;
  s_start_tilt = s_current_tilt;
  s_segment_start_tick = HAL_GetTick();
  s_hold_start_tick = s_segment_start_tick;
  clear_queue();
  apply_current_angle();
}

/* 函数作用：周期更新本模块状态，要求在主循环或任务中反复调用。 */
void Gimbal_Update(uint32_t now_ms)
{
  Gimbal_Keyframe *frame;
  uint32_t elapsed;

  if (s_moving == 0u)
  {
    return;
  }

  frame = &s_queue[s_queue_index];

  if (s_holding != 0u)
  {
    if ((now_ms - s_hold_start_tick) >= frame->hold_ms)
    {
      advance_keyframe(now_ms);
    }
    return;
  }

  elapsed = now_ms - s_segment_start_tick;
  if (elapsed >= frame->duration_ms)
  {
    s_current_pan = frame->pan;
    s_current_tilt = frame->tilt;
    apply_current_angle();

    if (frame->hold_ms > 0u)
    {
      s_holding = 1u;
      s_hold_start_tick = now_ms;
    }
    else
    {
      advance_keyframe(now_ms);
    }
    return;
  }

  s_current_pan = Easing_InterpolateU8(s_start_pan, frame->pan, (uint16_t)elapsed, frame->duration_ms, frame->easing);
  s_current_tilt = Easing_InterpolateU8(s_start_tilt, frame->tilt, (uint16_t)elapsed, frame->duration_ms, frame->easing);
  apply_current_angle();
}

/* 函数作用：停止本模块当前动作，并把状态切回安全/空闲。 */
void Gimbal_Stop(void)
{
  clear_queue();
  apply_current_angle();
}

/* 函数作用：让云台按指定时间平滑回到中心角度。 */
void Gimbal_Center(uint16_t duration_ms)
{
  clear_queue();
  push_keyframe(GIMBAL_CENTER_PAN, GIMBAL_CENTER_TILT, duration_ms, 0u, EASING_IN_OUT);
  start_queue(GIMBAL_ACTION_CENTER);
}

/* 函数作用：设置本模块内部状态或输出目标。 */
void Gimbal_SetPan(uint8_t pan, uint16_t duration_ms, Easing_Type easing)
{
  clear_queue();
  push_keyframe(pan, s_current_tilt, duration_ms, 0u, easing);
  start_queue(GIMBAL_ACTION_CENTER);
}

/* 函数作用：设置本模块内部状态或输出目标。 */
void Gimbal_SetTilt(uint8_t tilt, uint16_t duration_ms, Easing_Type easing)
{
  clear_queue();
  push_keyframe(s_current_pan, tilt, duration_ms, 0u, easing);
  start_queue(GIMBAL_ACTION_CENTER);
}

/* 函数作用：设置本模块内部状态或输出目标。 */
void Gimbal_SetPanTilt(uint8_t pan, uint8_t tilt, uint16_t duration_ms, Easing_Type easing)
{
  clear_queue();
  push_keyframe(pan, tilt, duration_ms, 0u, easing);
  start_queue(GIMBAL_ACTION_CENTER);
}

/* 函数作用：根据动作编号装载云台关键帧队列并启动预设动作。 */
uint8_t Gimbal_StartAction(uint8_t action_id)
{
  uint8_t left = offset_u8(s_current_pan, -25, GIMBAL_PAN_MIN, GIMBAL_PAN_MAX);
  uint8_t right = offset_u8(s_current_pan, 25, GIMBAL_PAN_MIN, GIMBAL_PAN_MAX);

  if (action_id > GIMBAL_ACTION_MAX)
  {
    return 0u;
  }

  clear_queue();

  switch (action_id)
  {
    case GIMBAL_ACTION_CENTER:
      push_keyframe(GIMBAL_CENTER_PAN, GIMBAL_CENTER_TILT, 600u, 0u, EASING_IN_OUT);
      break;

    case GIMBAL_ACTION_LOOK_LEFT_SOFT:
      push_keyframe(left, offset_u8(s_current_tilt, 3, GIMBAL_TILT_MIN, GIMBAL_TILT_MAX), 500u, 200u, EASING_SMOOTH_STEP);
      break;

    case GIMBAL_ACTION_LOOK_RIGHT_SOFT:
      push_keyframe(right, offset_u8(s_current_tilt, 3, GIMBAL_TILT_MIN, GIMBAL_TILT_MAX), 500u, 200u, EASING_SMOOTH_STEP);
      break;

    case GIMBAL_ACTION_NOD_YES:
      push_keyframe(90u, 65u, 300u, 80u, EASING_IN_OUT);
      push_keyframe(90u, 105u, 350u, 80u, EASING_IN_OUT);
      push_keyframe(90u, 90u, 300u, 0u, EASING_IN_OUT);
      break;

    case GIMBAL_ACTION_SHAKE_NO:
      push_keyframe(65u, 90u, 280u, 60u, EASING_IN_OUT);
      push_keyframe(115u, 90u, 360u, 60u, EASING_IN_OUT);
      push_keyframe(75u, 90u, 300u, 60u, EASING_IN_OUT);
      push_keyframe(90u, 90u, 260u, 0u, EASING_IN_OUT);
      break;

    case GIMBAL_ACTION_SCAN_SLOW:
      push_keyframe(60u, 85u, 800u, 150u, EASING_SMOOTH_STEP);
      push_keyframe(120u, 95u, 1200u, 150u, EASING_SMOOTH_STEP);
      push_keyframe(90u, 90u, 700u, 0u, EASING_SMOOTH_STEP);
      break;

    case GIMBAL_ACTION_CURIOUS_TILT:
      push_keyframe(78u, 72u, 450u, 100u, EASING_SMOOTH_STEP);
      push_keyframe(96u, 85u, 550u, 120u, EASING_SMOOTH_STEP);
      push_keyframe(90u, 90u, 400u, 0u, EASING_SMOOTH_STEP);
      break;

    case GIMBAL_ACTION_LOOK_UP_SOFT:
      push_keyframe(s_current_pan, offset_u8(s_current_tilt, -18, GIMBAL_TILT_MIN, GIMBAL_TILT_MAX), 500u, 180u, EASING_SMOOTH_STEP);
      break;

    case GIMBAL_ACTION_LOOK_DOWN_SOFT:
      push_keyframe(s_current_pan, offset_u8(s_current_tilt, 22, GIMBAL_TILT_MIN, GIMBAL_TILT_MAX), 550u, 180u, EASING_SMOOTH_STEP);
      break;

    case GIMBAL_ACTION_SCAN_FAST:
      push_keyframe(45u, 90u, 420u, 60u, EASING_IN_OUT);
      push_keyframe(135u, 90u, 650u, 60u, EASING_IN_OUT);
      push_keyframe(90u, 90u, 380u, 0u, EASING_IN_OUT);
      break;

    case GIMBAL_ACTION_SLEEPY:
      push_keyframe(88u, 112u, 700u, 120u, EASING_SMOOTH_STEP);
      push_keyframe(92u, 132u, 900u, 120u, EASING_SMOOTH_STEP);
      push_keyframe(90u, 140u, 900u, 0u, EASING_SMOOTH_STEP);
      break;

    case GIMBAL_ACTION_WAKE_UP:
      push_keyframe(80u, 120u, 450u, 100u, EASING_IN_OUT);
      push_keyframe(100u, 88u, 550u, 100u, EASING_IN_OUT);
      push_keyframe(90u, 90u, 450u, 0u, EASING_IN_OUT);
      break;

    case GIMBAL_ACTION_SHY_LOOK_DOWN:
      push_keyframe(84u, 105u, 500u, 100u, EASING_SMOOTH_STEP);
      push_keyframe(80u, 120u, 700u, 0u, EASING_SMOOTH_STEP);
      break;

    case GIMBAL_ACTION_HAPPY_WIGGLE:
      push_keyframe(80u, 80u, 240u, 40u, EASING_IN_OUT);
      push_keyframe(100u, 95u, 280u, 40u, EASING_IN_OUT);
      push_keyframe(82u, 85u, 260u, 40u, EASING_IN_OUT);
      push_keyframe(98u, 92u, 260u, 40u, EASING_IN_OUT);
      push_keyframe(90u, 90u, 260u, 0u, EASING_IN_OUT);
      break;

    case GIMBAL_ACTION_THINKING_MOTION:
      push_keyframe(84u, 88u, 700u, 120u, EASING_SMOOTH_STEP);
      push_keyframe(96u, 92u, 850u, 120u, EASING_SMOOTH_STEP);
      push_keyframe(86u, 91u, 800u, 120u, EASING_SMOOTH_STEP);
      push_keyframe(94u, 87u, 750u, 120u, EASING_SMOOTH_STEP);
      push_keyframe(90u, 90u, 650u, 0u, EASING_SMOOTH_STEP);
      break;

    default:
      return 0u;
  }

  start_queue(action_id);
  return 1u;
}

/* 函数作用：读取并返回本模块缓存的状态或配置值。 */
uint8_t Gimbal_GetPan(void)
{
  return s_current_pan;
}

/* 函数作用：读取并返回本模块缓存的状态或配置值。 */
uint8_t Gimbal_GetTilt(void)
{
  return s_current_tilt;
}

/* 函数作用：判断当前状态是否满足对应条件。 */
uint8_t Gimbal_IsMoving(void)
{
  return s_moving;
}

/* 函数作用：读取并返回本模块缓存的状态或配置值。 */
uint8_t Gimbal_GetActionId(void)
{
  return s_action_id;
}
