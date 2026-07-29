#ifndef BSP_ULTRASONIC_H
#define BSP_ULTRASONIC_H

/*
 * 模块：bsp_ultrasonic
 * 负责什么：驱动 HC-SR04 超声波测距，定期触发测距并保存最近一次距离。
 * 用到什么硬件：TRIG=PA1 普通输出，ECHO=PA0/TIM2_CH1 输入捕获，TIM2 计数频率 1MHz。
 * 初始化函数：BspUltrasonic_Init()。
 * 主循环调用：BspUltrasonic_Update(HAL_GetTick())，内部每 100ms 自动触发一次测距。
 * 怎么测试：上电后请求 STATUS_SENSOR，dist_l/dist_h 为 cm x10；遮挡到 15cm 内时安全模块会停车。
 * 失败先检查：TRIG/ECHO 是否接对、ECHO 是否为 3.3V 电平、TIM2 是否启动输入捕获、中断是否进入。
 */

#include "main.h"
#include <stdint.h>

#define ULTRASONIC_SAMPLE_INTERVAL_MS 100u
#define ULTRASONIC_ECHO_TIMEOUT_MS 35u
#define ULTRASONIC_NEAR_STOP_CM_X10 150u
#define ULTRASONIC_DISTANCE_INVALID 0xFFFFu

typedef struct
{
  uint8_t valid;
  uint8_t timeout;
  uint16_t distance_cm_x10;
  uint16_t echo_us;
  uint32_t last_update_tick;
} Ultrasonic_Data;

void BspUltrasonic_Init(void);
void BspUltrasonic_Update(uint32_t now_ms);
Ultrasonic_Data BspUltrasonic_GetData(void);
uint8_t BspUltrasonic_IsTooClose(void);

#endif
