# 自然动作与非阻塞行为设计

## 1. Gimbal Motion Engine

模块位置：

- STM32：`User/App/gimbal_control.c/.h`
- STM32：`User/Utils/easing.c/.h`
- STM32：`User/BSP/bsp_servo.c/.h`

职责：

- 管理 Pan/Tilt 当前角度和目标角度。
- 所有动作使用插值，不能直接跳角度。
- 支持关键帧、hold、动作中断、动作队列。
- Tilt 硬限制 30 到 150。

核心接口：

```c
void Gimbal_Init(void);
void Gimbal_Update(uint32_t now_ms);
void Gimbal_Stop(void);
void Gimbal_Center(uint16_t duration_ms);
void Gimbal_MoveTo(uint8_t pan, uint8_t tilt, uint16_t duration_ms, easing_type_t easing);
void Gimbal_StartAction(gimbal_action_t action);
uint8_t Gimbal_GetPan(void);
uint8_t Gimbal_GetTilt(void);
uint8_t Gimbal_IsMoving(void);
```

关键帧结构：

```c
typedef struct {
    uint8_t pan;
    uint8_t tilt;
    uint16_t duration_ms;
    uint16_t hold_ms;
    easing_type_t easing;
} gimbal_keyframe_t;
```

主循环必须持续调用 `Gimbal_Update(HAL_GetTick())`。动作中不得使用长时间 `HAL_Delay`。

## 2. 云台动作

| 动作 | 说明 |
| --- | --- |
| `center` | Pan=90，Tilt=90，平滑回中 |
| `look_left_soft` | Pan 当前角度减 25，Tilt 微调 |
| `look_right_soft` | Pan 当前角度加 25，Tilt 微调 |
| `nod_yes` | Tilt 90 -> 65 -> 105 -> 90 |
| `shake_no` | Pan 90 -> 65 -> 115 -> 75 -> 90 |
| `curious_tilt` | Pan 和 Tilt 小幅交错变化 |
| `thinking_motion` | 慢速小幅摆动 |
| `happy_wiggle` | 快速小幅摆动，配合彩灯 |
| `shy_look_down` | Tilt 到 120，Pan 到 80 |
| `sleep_pose` | Tilt 慢到 140，灯光变暗 |
| `wake_up` | Tilt 140 -> 90，Pan 左右看 |
| `scan_slow` | Pan 60 -> 120 -> 90 |
| `scan_fast` | Pan 45 -> 135 -> 90 |

## 3. Motor Ramp Engine

模块位置：

- STM32：`User/App/car_control.c/.h`
- STM32：`User/BSP/bsp_motor.c/.h`

职责：

- 电机速度不能从 0 突然跳到高速度。
- 所有运动命令只设置方向、目标速度、持续时间。
- `Motor_Update(now_ms)` 负责速度斜坡。
- STOP 立即清 PWM。

核心接口：

```c
void Motor_Init(void);
void Motor_Stop(void);
void Motor_Move(motor_direction_t direction, uint8_t speed, uint16_t duration_ms);
void Motor_RampToSpeed(uint8_t target_speed, uint16_t ramp_ms);
void Motor_Update(uint32_t now_ms);
uint8_t Motor_GetCurrentSpeed(void);
motor_state_t Motor_GetState(void);
```

安全规则：

- 默认速度 0。
- 默认目标速度 0。
- 通信超时、急停、距离过近立即停车。
- 速度上限由 STM32 本地常量限制。

## 4. Behavior Engine

模块位置：

- ESP32：`components/behavior_engine/`
- STM32：只执行命令和本地安全，不理解 AI 行为。

职责：

- 管理行为状态，不阻塞主循环。
- 行为通过 `Behavior_Start`、`Behavior_Update`、`Behavior_Stop` 执行。
- 急停、避障、通信超时可随时打断。

核心接口：

```c
void Behavior_Init(void);
void Behavior_Start(robot_behavior_name_t name);
void Behavior_Update(uint32_t now_ms);
void Behavior_Stop(void);
robot_behavior_name_t Behavior_GetCurrent(void);
```

行为实现规则：

- `find_user`：停车、云台扫描、1 FPS 上传图像。
- `follow_user`：本地视觉做实时控制，Qwen 低频判断。
- `patrol`：低速前进、遇障停车、后退、观察、转向。
- `cute`：组合云台动作、灯效、语音。
- `sleep`：停车、低头、停止高频图像上传。
- `wake_up`：抬头、灯光亮起、播放提示音。

