#ifndef APP_MAIN_H
#define APP_MAIN_H

/*
 * 模块：app_main
 * 负责什么：STM32 正式业务入口，连接 CubeMX 初始化后的 HAL 外设和 User 层模块。
 * 用到什么硬件：USART1 DMA 接收 ESP32-P4 帧，TIM3/GPIO 用于确保电机停车。
 * 初始化函数：App_Main_Init()，在 MX_* 初始化完成后调用。
 * 主循环调用：App_Main_Loop()，必须在 while(1) 中高频调用，不能阻塞。
 * 怎么测试：烧录后发送 HEARTBEAT/REQUEST_STATUS/STOP 帧，观察串口回包。
 * 失败先检查：MDK 是否默认关闭 ENABLE_STM32_TEST_MODE、USART1 DMA 是否初始化、User 文件是否加入工程。
 */

void App_Main_Init(void);
void App_Main_Loop(void);

#endif
