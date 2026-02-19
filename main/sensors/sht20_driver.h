/**
 * @file sht20_driver.h
 * @brief SHT20 I2C temperature/humidity sensor driver
 *
 * I2C Address: 0x40
 */

#ifndef SHT20_DRIVER_H
#define SHT20_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHT20_I2C_ADDR  0x40

/**
 * @brief Probe for SHT20 on the I2C bus
 */
esp_err_t sht20_probe(i2c_master_bus_handle_t bus);

/**
 * @brief Initialize SHT20 device handle
 */
esp_err_t sht20_init(i2c_master_bus_handle_t bus);

/**
 * @brief Read temperature and humidity
 */
esp_err_t sht20_read(float *temperature, float *humidity);

#ifdef __cplusplus
}
#endif

#endif /* SHT20_DRIVER_H */
