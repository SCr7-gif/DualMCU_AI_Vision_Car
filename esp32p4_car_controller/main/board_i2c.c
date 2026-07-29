#include "board_i2c.h"

#include "board_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static const char *TAG = "board_i2c";
static i2c_master_bus_handle_t s_i2c_bus;

esp_err_t board_i2c_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_i2c_bus), TAG, "i2c_new_master_bus failed");
    ESP_LOGI(TAG, "I2C master bus ready: SDA=%d SCL=%d freq=%d", BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO, BOARD_I2C_FREQ_HZ);
    return ESP_OK;
}

esp_err_t board_i2c_deinit(void)
{
    if (s_i2c_bus == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = i2c_del_master_bus(s_i2c_bus);
    if (ret == ESP_OK) {
        s_i2c_bus = NULL;
        ESP_LOGI(TAG, "I2C master bus released");
    }
    return ret;
}

i2c_port_num_t board_i2c_get_port(void)
{
    return BOARD_I2C_PORT;
}

i2c_master_bus_handle_t board_i2c_get_bus_handle(void)
{
    return s_i2c_bus;
}

static esp_err_t with_device(uint8_t dev_addr, i2c_master_dev_handle_t *out)
{
    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "bus init failed");
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, out);
}

void board_i2c_scan(void)
{
    ESP_LOGI(TAG, "I2C scan start");
    if (board_i2c_init() != ESP_OK) {
        ESP_LOGW(TAG, "I2C scan skipped, bus not ready");
        return;
    }

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(s_i2c_bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found: 0x%02X", addr);
        }
    }
    ESP_LOGI(TAG, "I2C scan done");
}

esp_err_t board_i2c_write_reg8(uint8_t dev_addr, uint8_t reg, uint8_t value)
{
    i2c_master_dev_handle_t dev = NULL;
    ESP_RETURN_ON_ERROR(with_device(dev_addr, &dev), TAG, "add device 0x%02X failed", dev_addr);
    uint8_t data[2] = {reg, value};
    esp_err_t ret = i2c_master_transmit(dev, data, sizeof(data), 100);
    i2c_master_bus_rm_device(dev);
    return ret;
}

esp_err_t board_i2c_read_reg8(uint8_t dev_addr, uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t dev = NULL;
    ESP_RETURN_ON_ERROR(with_device(dev_addr, &dev), TAG, "add device 0x%02X failed", dev_addr);
    esp_err_t ret = i2c_master_transmit_receive(dev, &reg, 1, value, 1, 100);
    i2c_master_bus_rm_device(dev);
    return ret;
}

esp_err_t board_i2c_read_reg16(uint8_t dev_addr, uint16_t reg, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_dev_handle_t dev = NULL;
    ESP_RETURN_ON_ERROR(with_device(dev_addr, &dev), TAG, "add device 0x%02X failed", dev_addr);
    uint8_t reg_buf[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xff)};
    esp_err_t ret = i2c_master_transmit_receive(dev, reg_buf, sizeof(reg_buf), value, 1, 100);
    i2c_master_bus_rm_device(dev);
    return ret;
}
