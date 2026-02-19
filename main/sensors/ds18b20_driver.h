/**
 * @file ds18b20_driver.h
 * @brief DS18B20 1-Wire temperature sensor driver
 *
 * Uses ESP-IDF onewire_bus and ds18b20 components.
 */

#ifndef DS18B20_DRIVER_H
#define DS18B20_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default 1-Wire data pin (can be changed)
#define DS18B20_GPIO    23

/**
 * @brief Initialize DS18B20 sensor
 *
 * @param gpio_num GPIO pin for 1-Wire data line
 * @return ESP_OK on success
 */
esp_err_t ds18b20_init(int gpio_num);

/**
 * @brief Read temperature from DS18B20
 *
 * @param temperature Pointer to store temperature in °C
 * @return ESP_OK on success
 */
esp_err_t ds18b20_read(float *temperature);

/**
 * @brief Check if DS18B20 is connected
 */
bool ds18b20_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* DS18B20_DRIVER_H */
