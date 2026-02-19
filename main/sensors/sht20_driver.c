/**
 * @file sht20_driver.c
 * @brief SHT20 I2C temperature/humidity sensor driver
 *
 * Protocol:
 * - Trigger measurement (no hold): Temp=0xF3, Hum=0xF5
 * - Wait for conversion (~85ms for 14-bit temp, ~29ms for 12-bit hum)
 * - Read 3 bytes (MSB, LSB, CRC)
 */

#include "sht20_driver.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SHT20";

// SHT20 commands
#define SHT20_CMD_TEMP_NOHOLD   0xF3
#define SHT20_CMD_HUM_NOHOLD    0xF5
#define SHT20_CMD_SOFT_RESET    0xFE

static i2c_master_dev_handle_t sht20_dev = NULL;

esp_err_t sht20_probe(i2c_master_bus_handle_t bus)
{
    return i2c_master_probe(bus, SHT20_I2C_ADDR, 100);
}

esp_err_t sht20_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT20_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &sht20_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SHT20 device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Soft reset
    uint8_t cmd = SHT20_CMD_SOFT_RESET;
    i2c_master_transmit(sht20_dev, &cmd, 1, 100);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "SHT20 initialized");
    return ESP_OK;
}

static esp_err_t sht20_read_raw(uint8_t cmd, uint16_t *raw)
{
    esp_err_t ret;

    // Send measurement command
    ret = i2c_master_transmit(sht20_dev, &cmd, 1, 100);
    if (ret != ESP_OK) {
        return ret;
    }

    // Wait for conversion
    vTaskDelay(pdMS_TO_TICKS(cmd == SHT20_CMD_TEMP_NOHOLD ? 85 : 30));

    // Read 3 bytes: MSB, LSB, CRC
    uint8_t data[3] = {0};
    ret = i2c_master_receive(sht20_dev, data, 3, 100);
    if (ret != ESP_OK) {
        return ret;
    }

    // Clear status bits (last 2 bits of LSB)
    *raw = ((uint16_t)data[0] << 8) | (data[1] & 0xFC);
    return ESP_OK;
}

esp_err_t sht20_read(float *temperature, float *humidity)
{
    if (!sht20_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t raw_temp, raw_hum;
    esp_err_t ret;

    // Read temperature
    ret = sht20_read_raw(SHT20_CMD_TEMP_NOHOLD, &raw_temp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature: %s", esp_err_to_name(ret));
        return ret;
    }

    // Read humidity
    ret = sht20_read_raw(SHT20_CMD_HUM_NOHOLD, &raw_hum);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read humidity: %s", esp_err_to_name(ret));
        return ret;
    }

    // Convert raw values
    // Temperature: -46.85 + 175.72 * raw / 65536
    *temperature = -46.85f + 175.72f * (float)raw_temp / 65536.0f;

    // Humidity: -6.0 + 125.0 * raw / 65536
    *humidity = -6.0f + 125.0f * (float)raw_hum / 65536.0f;

    // Clamp humidity
    if (*humidity < 0.0f) *humidity = 0.0f;
    if (*humidity > 100.0f) *humidity = 100.0f;

    return ESP_OK;
}
