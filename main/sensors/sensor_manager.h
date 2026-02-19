/**
 * @file sensor_manager.h
 * @brief Sensor manager for I2C temperature/humidity sensors and DS18B20
 *
 * Auto-detects I2C sensors: SHT20 (0x40), SHT3x (0x44), AM2315C/AHT20 (0x38)
 * Also supports DS18B20 1-Wire temperature sensor.
 */

#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sensor data structure
 */
typedef struct {
    float temperature;       // from I2C sensor (°C)
    float humidity;          // from I2C sensor (%)
    float ds18b20_temp;      // from DS18B20 (°C)
    float thermocouple_temp; // from MAX6675 thermocouple (°C)
    bool temp_hum_valid;     // true if I2C sensor data is valid
    bool ds18b20_valid;      // true if DS18B20 data is valid
    bool thermocouple_valid; // true if thermocouple data is valid
    uint32_t timestamp_ms;   // millis when last read
    char sensor_name[16];    // "SHT20", "SHT3x", "AM2315C", "None"
} sensor_data_t;

/**
 * @brief Initialize sensor manager
 *
 * Sets up I2C bus and auto-detects connected sensors.
 *
 * @return ESP_OK on success
 */
esp_err_t sensor_manager_init(void);

/**
 * @brief Read all sensors
 *
 * Reads I2C temp/humidity sensor and DS18B20 (if enabled).
 * Applies correction offsets from config.
 *
 * @return ESP_OK if at least one sensor read succeeded
 */
esp_err_t sensor_manager_read(void);

/**
 * @brief Get current sensor data
 *
 * Thread-safe access to the latest sensor readings.
 *
 * @param data Pointer to sensor_data_t to fill
 * @return ESP_OK on success
 */
esp_err_t sensor_manager_get_data(sensor_data_t *data);

/**
 * @brief Get I2C bus handle for shared bus access (OLED display)
 *
 * @return I2C master bus handle, or NULL if not initialized
 */
void *sensor_manager_get_i2c_bus(void);

/**
 * @brief Sensor reading FreeRTOS task
 *
 * Periodically reads sensors at the configured interval.
 * Should be created with xTaskCreatePinnedToCore on Core 0.
 *
 * @param param Unused
 */
void sensor_task(void *param);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_MANAGER_H */
