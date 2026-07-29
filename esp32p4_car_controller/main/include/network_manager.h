#pragma once

/*
 * 模块：network_manager
 * 负责什么：管理 ESP32-P4 的 Wi-Fi 联网和断线重连。
 * 用到什么硬件：ESP32-P4 Wi-Fi。
 * 初始化函数：network_manager_init()。
 * 主循环调用：不需要，事件回调自动维护连接状态。
 * 怎么测试：NVS 配置 Wi-Fi 后，串口日志应显示 got ip。
 * 失败先检查：NVS 中 wifi_ssid/wifi_pass 是否存在、路由器是否 2.4G/兼容、天线和信号是否正常。
 */

#include "esp_err.h"
#include <stdbool.h>

esp_err_t network_manager_init(void);
bool network_manager_is_connected(void);
bool network_manager_is_started(void);
bool network_manager_has_native_wifi(void);
const char *network_manager_backend_name(void);
const char *network_manager_ip_string(void);
