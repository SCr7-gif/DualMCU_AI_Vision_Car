#pragma once

/*
 * Web debug server.
 *
 * Starts the browser test page and HTTP APIs used to test STM32 commands,
 * audio tone/microphone, network state, and Qwen text prompts.
 */

#include "esp_err.h"
#include <stdbool.h>

esp_err_t web_debug_start(void);
bool web_debug_is_manual_mode(void);
