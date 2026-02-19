/**
 * @file sht3x_driver.c
 * @brief SHT30/SHT31/SHT40 I2C temperature/humidity sensor driver
 *
 * Protocol:
 * - Single shot measurement: 0x2400 (high repeatability)
 * - Wait for conversion (~15ms)
 * - Read 6 bytes: Temp MSB, Temp LSB, CRC, Hum MSB, Hum LSB, CRC
 */

#include "sht3x_driver.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SHT3X";

// SHT3x commands (MSB first)
#define SHT3X_CMD_MEASURE_HIGH_MSB   0x24
#define SHT3X_CMD_MEASURE_HIGH_LSB   0x00
#define SHT3X_CMD_SOFT_RESET_MSB     0x30
#define SHT3X_CMD_SOFT_RESET_LSB     0xA2

static i2c_master_dev_handle_t sht3x_dev = NULL;

esp_err_t sht3x_probe(i2c_master_bus_handle_t bus)
{
    return i2c_master_probe(bus, SHT3X_I2C_ADDR, 100);
}

esp_err_t sht3x_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT3X_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &sht3x_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SHT3x device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Soft reset
    uint8_t cmd[2] = { SHT3X_CMD_SOFT_RESET_MSB, SHT3X_CMD_SOFT_RESET_LSB };
    i2c_master_transmit(sht3x_dev, cmd, 2, 100);
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "SHT3x initialized");
    return ESP_OK;
}

esp_err_t sht3x_read(float *temperature, float *humidity)
{
    if (!sht3x_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;

    // Start single-shot measurement (high repeatability)
    uint8_t cmd[2] = { SHT3X_CMD_MEASURE_HIGH_MSB, SHT3X_CMD_MEASURE_HIGH_LSB };
    ret = i2c_master_transmit(sht3x_dev, cmd, 2, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start measurement: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for conversion
    vTaskDelay(pdMS_TO_TICKS(20));

    // Read 6 bytes: Temp MSB, Temp LSB, CRC, Hum MSB, Hum LSB, CRC
    uint8_t data[6] = {0};
    ret = i2c_master_receive(sht3x_dev, data, 6, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data: %s", esp_err_to_name(ret));
        return ret;
    }

    // Convert raw values
    uint16_t raw_temp = ((uint16_t)data[0] << 8) | data[1];
    uint16_t raw_hum = ((uint16_t)data[3] << 8) | data[4];

    // Temperature: -45 + 175 * raw / 65535
    *temperature = -45.0f + 175.0f * (float)raw_temp / 65535.0f;

    // Humidity: 100 * raw / 65535
    *humidity = 100.0f * (float)raw_hum / 65535.0f;

    // Clamp humidity
    if (*humidity < 0.0f) *humidity = 0.0f;
    if (*humidity > 100.0f) *humidity = 100.0f;

    return ESP_OK;
}
