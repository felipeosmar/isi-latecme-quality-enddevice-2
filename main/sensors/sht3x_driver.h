/**
 * @file sht3x_driver.h
 * @brief SHT30/SHT31/SHT40 I2C temperature/humidity sensor driver
 *
 * I2C Address: 0x44 (default) or 0x45
 */

#ifndef SHT3X_DRIVER_H
#define SHT3X_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHT3X_I2C_ADDR  0x44

/**
 * @brief Probe for SHT3x on the I2C bus
 */
esp_err_t sht3x_probe(i2c_master_bus_handle_t bus);

/**
 * @brief Initialize SHT3x device handle
 */
esp_err_t sht3x_init(i2c_master_bus_handle_t bus);

/**
 * @brief Read temperature and humidity
 */
esp_err_t sht3x_read(float *temperature, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* SHT3X_DRIVER_H */
