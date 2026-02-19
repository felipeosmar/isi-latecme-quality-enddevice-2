/**
 * @file am2315c_driver.c
 * @brief AM2315C / AHT20 I2C temperature/humidity sensor driver
 *
 * Protocol:
 * - Initialize: send 0xBE 0x08 0x00
 * - Trigger measurement: send 0xAC 0x33 0x00
 * - Wait for conversion (~80ms)
 * - Read 7 bytes: status, hum[19:12], hum[11:4], hum[3:0]|temp[19:16], temp[15:8], temp[7:0], CRC
 */

#include "am2315c_driver.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AM2315C";

// AM2315C/AHT20 commands
#define AM2315C_CMD_INIT_1      0xBE
#define AM2315C_CMD_INIT_2      0x08
#define AM2315C_CMD_INIT_3      0x00
#define AM2315C_CMD_MEASURE_1   0xAC
#define AM2315C_CMD_MEASURE_2   0x33
#define AM2315C_CMD_MEASURE_3   0x00
#define AM2315C_CMD_SOFT_RST    0xBA

#define AM2315C_STATUS_BUSY     0x80
#define AM2315C_STATUS_CAL      0x08

static i2c_master_dev_handle_t am2315c_dev = NULL;

esp_err_t am2315c_probe(i2c_master_bus_handle_t bus)
{
    return i2c_master_probe(bus, AM2315C_I2C_ADDR, 100);
}

esp_err_t am2315c_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AM2315C_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &am2315c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add AM2315C device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for power-on (at least 40ms)
    vTaskDelay(pdMS_TO_TICKS(50));

    // Check status - if not calibrated, send init command
    uint8_t status = 0;
    ret = i2c_master_receive(am2315c_dev, &status, 1, 100);
    if (ret == ESP_OK && !(status & AM2315C_STATUS_CAL)) {
        uint8_t init_cmd[3] = { AM2315C_CMD_INIT_1, AM2315C_CMD_INIT_2, AM2315C_CMD_INIT_3 };
        i2c_master_transmit(am2315c_dev, init_cmd, 3, 100);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "AM2315C/AHT20 initialized");
    return ESP_OK;
}

esp_err_t am2315c_read(float *temperature, float *humidity)
{
    if (!am2315c_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;

    // Trigger measurement
    uint8_t cmd[3] = { AM2315C_CMD_MEASURE_1, AM2315C_CMD_MEASURE_2, AM2315C_CMD_MEASURE_3 };
    ret = i2c_master_transmit(am2315c_dev, cmd, 3, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to trigger measurement: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for conversion
    vTaskDelay(pdMS_TO_TICKS(80));

    // Read 7 bytes
    uint8_t data[7] = {0};
    ret = i2c_master_receive(am2315c_dev, data, 7, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data: %s", esp_err_to_name(ret));
        return ret;
    }

    // Check if still busy
    if (data[0] & AM2315C_STATUS_BUSY) {
        ESP_LOGW(TAG, "Sensor still busy, retrying...");
        vTaskDelay(pdMS_TO_TICKS(50));
        ret = i2c_master_receive(am2315c_dev, data, 7, 100);
        if (ret != ESP_OK || (data[0] & AM2315C_STATUS_BUSY)) {
            return ESP_ERR_TIMEOUT;
        }
    }

    // Parse humidity (20-bit value)
    uint32_t raw_hum = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);

    // Parse temperature (20-bit value)
    uint32_t raw_temp = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

    // Convert
    *humidity = (float)raw_hum / 1048576.0f * 100.0f;
    *temperature = (float)raw_temp / 1048576.0f * 200.0f - 50.0f;

    // Clamp humidity
    if (*humidity < 0.0f) *humidity = 0.0f;
    if (*humidity > 100.0f) *humidity = 100.0f;

    return ESP_OK;
}
