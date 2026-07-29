#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t board_i2c_init(void);
esp_err_t board_i2c_deinit(void);
i2c_port_num_t board_i2c_get_port(void);
i2c_master_bus_handle_t board_i2c_get_bus_handle(void);
void board_i2c_scan(void);
esp_err_t board_i2c_write_reg8(uint8_t dev_addr, uint8_t reg, uint8_t value);
esp_err_t board_i2c_read_reg8(uint8_t dev_addr, uint8_t reg, uint8_t *value);
esp_err_t board_i2c_read_reg16(uint8_t dev_addr, uint16_t reg, uint8_t *value);
