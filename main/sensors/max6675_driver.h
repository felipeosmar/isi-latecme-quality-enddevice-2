/**
 * @file max6675_driver.h
 * @brief MAX6675 thermocouple temperature sensor driver (SPI bit-bang)
 *
 * Uses software SPI (bit-bang) to avoid conflict with hardware SPI (SX1276 LoRa).
 * Type K thermocouple, 0-200°C range.
 */

#ifndef MAX6675_DRIVER_H
#define MAX6675_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default GPIO pins (HW-550 module)
#define MAX6675_SCK_GPIO    33
#define MAX6675_SO_GPIO     27
#define MAX6675_CS_GPIO     32

/**
 * @brief Initialize MAX6675 sensor (SPI bit-bang)
 *
 * @param sck_gpio GPIO pin for SCK (clock)
 * @param so_gpio GPIO pin for SO (MISO / data out)
 * @param cs_gpio GPIO pin for CS (chip select)
 * @return ESP_OK on success
 */
esp_err_t max6675_init(int sck_gpio, int so_gpio, int cs_gpio);

/**
 * @brief Read temperature from MAX6675
 *
 * @param temperature Pointer to store temperature in °C
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if thermocouple is open
 */
esp_err_t max6675_read(float *temperature);

/**
 * @brief Check if thermocouple is connected
 *
 * @return true if thermocouple is connected (no open circuit detected)
 */
bool max6675_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* MAX6675_DRIVER_H */
