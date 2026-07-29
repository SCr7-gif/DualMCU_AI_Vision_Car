#pragma once

#include "esp_err.h"
#include <stddef.h>

esp_err_t board_diagnostics_init(void);
void board_diagnostics_log_summary(void);
int board_diagnostics_write_json(char *out, size_t out_size);
void board_diagnostics_print_console(void);
