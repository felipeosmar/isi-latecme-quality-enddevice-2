/**
 * @file ds18b20_driver.c
 * @brief DS18B20 1-Wire temperature sensor driver
 *
 * Uses ESP-IDF onewire_bus RMT-based driver for reliable 1-Wire timing.
 * Supports a single DS18B20 on the bus.
 */

#include "ds18b20_driver.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "onewire_bus.h"
#include "ds18b20.h"

static const char *TAG = "DS18B20";

static onewire_bus_handle_t owb = NULL;
static ds18b20_device_handle_t ds18b20_handle = NULL;
static bool connected = false;

esp_err_t ds18b20_init(int gpio_num)
{
    esp_err_t ret;

    // Configure 1-Wire bus using RMT
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = gpio_num,
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10,
    };

    ret = onewire_new_bus_rmt(&bus_config, &rmt_config, &owb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create 1-Wire bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Search for DS18B20 devices
    onewire_device_iter_handle_t iter = NULL;
    ret = onewire_new_device_iter(owb, &iter);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create device iterator: %s", esp_err_to_name(ret));
        return ret;
    }

    onewire_device_t device;
    ret = onewire_device_iter_get_next(iter, &device);
    onewire_del_device_iter(iter);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No DS18B20 found on GPIO%d", gpio_num);
        connected = false;
        return ESP_ERR_NOT_FOUND;
    }

    // Create DS18B20 device handle
    ds18b20_config_t ds_cfg = {};
    ret = ds18b20_new_device(&device, &ds_cfg, &ds18b20_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DS18B20 device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set 12-bit resolution
    ret = ds18b20_set_resolution(ds18b20_handle, DS18B20_RESOLUTION_12B);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set resolution: %s", esp_err_to_name(ret));
    }

    connected = true;
    ESP_LOGI(TAG, "DS18B20 initialized on GPIO%d", gpio_num);
    return ESP_OK;
}

esp_err_t ds18b20_read(float *temperature)
{
    if (!ds18b20_handle || !connected) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;

    // Trigger conversion
    ret = ds18b20_trigger_temperature_conversion(ds18b20_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to trigger conversion: %s", esp_err_to_name(ret));
        connected = false;
        return ret;
    }

    // Wait for conversion (750ms for 12-bit)
    vTaskDelay(pdMS_TO_TICKS(800));

    // Read temperature
    ret = ds18b20_get_temperature(ds18b20_handle, temperature);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature: %s", esp_err_to_name(ret));
        connected = false;
        return ret;
    }

    return ESP_OK;
}

bool ds18b20_is_connected(void)
{
    return connected;
}
