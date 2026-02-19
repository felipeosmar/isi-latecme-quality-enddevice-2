/**
 * @file am2315c_driver.h
 * @brief AM2315C / AHT20 I2C temperature/humidity sensor driver
 *
 * I2C Address: 0x38
 */

#ifndef AM2315C_DRIVER_H
#define AM2315C_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AM2315C_I2C_ADDR  0x38

/**
 * @brief Probe for AM2315C/AHT20 on the I2C bus
 */
esp_err_t am2315c_probe(i2c_master_bus_handle_t bus);

/**
 * @brief Initialize AM2315C device handle
 */
esp_err_t am2315c_init(i2c_master_bus_handle_t bus);

/**
 * @brief Read temperature and humidity
 */
esp_err_t am2315c_read(float *temperature, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* AM2315C_DRIVER_H */
