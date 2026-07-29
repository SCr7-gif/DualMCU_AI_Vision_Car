#ifndef EASING_H
#define EASING_H

/*
 * 模块：easing
 * 负责什么：给电机/云台等动作提供简单整数缓动插值，避免角度或速度突然跳变。
 * 用到什么硬件：不直接使用硬件，只做数学计算。
 * 初始化函数：无。
 * 主循环调用：由 gimbal_control 在 Update 中按需调用。
 * 怎么测试：输入起点、终点、持续时间，观察输出从起点平滑变化到终点。
 * 失败先检查：duration_ms 是否为 0，elapsed_ms 是否超过 duration_ms，缓动类型是否传对。
 */

#include <stdint.h>

typedef enum
{
  EASING_LINEAR = 0,
  EASING_IN_OUT = 1,
  EASING_SMOOTH_STEP = 2
} Easing_Type;

uint8_t Easing_InterpolateU8(uint8_t start, uint8_t end, uint16_t elapsed_ms, uint16_t duration_ms, Easing_Type type);

#endif
