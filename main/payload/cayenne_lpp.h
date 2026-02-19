/**
 * @file cayenne_lpp.h
 * @brief CayenneLPP payload encoder for LoRaWAN
 *
 * Encodes sensor data in CayenneLPP format for automatic decoding
 * by ChirpStack's built-in CayenneLPP codec.
 *
 * Format: [Channel][Type][Data...]
 */

#ifndef CAYENNE_LPP_H
#define CAYENNE_LPP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// CayenneLPP data types
#define LPP_DIGITAL_INPUT       0x00  // 1 byte
#define LPP_DIGITAL_OUTPUT      0x01  // 1 byte
#define LPP_ANALOG_INPUT        0x02  // 2 bytes (0.01 signed)
#define LPP_ANALOG_OUTPUT       0x03  // 2 bytes (0.01 signed)
#define LPP_LUMINOSITY          0x65  // 2 bytes (1 lux unsigned)
#define LPP_PRESENCE            0x66  // 1 byte
#define LPP_TEMPERATURE         0x67  // 2 bytes (0.1°C signed)
#define LPP_RELATIVE_HUMIDITY   0x68  // 1 byte (0.5% unsigned)
#define LPP_BAROMETRIC_PRESSURE 0x73  // 2 bytes (0.1 hPa unsigned)

// Max payload size (safe for DR0/SF12)
#define LPP_MAX_SIZE            51

/**
 * @brief CayenneLPP encoder context
 */
typedef struct {
    uint8_t buffer[LPP_MAX_SIZE];
    size_t cursor;
} cayenne_lpp_t;

/**
 * @brief Reset the LPP encoder
 */
void cayenne_lpp_reset(cayenne_lpp_t *lpp);

/**
 * @brief Add temperature value
 *
 * @param lpp Encoder context
 * @param channel Channel number (0-255)
 * @param celsius Temperature in °C (resolution: 0.1°C)
 * @return 0 on success, -1 if buffer full
 */
int cayenne_lpp_add_temperature(cayenne_lpp_t *lpp, uint8_t channel, float celsius);

/**
 * @brief Add relative humidity value
 *
 * @param lpp Encoder context
 * @param channel Channel number (0-255)
 * @param percent Humidity in % (resolution: 0.5%)
 * @return 0 on success, -1 if buffer full
 */
int cayenne_lpp_add_humidity(cayenne_lpp_t *lpp, uint8_t channel, float percent);

/**
 * @brief Add analog input value
 *
 * @param lpp Encoder context
 * @param channel Channel number (0-255)
 * @param value Analog value (resolution: 0.01)
 * @return 0 on success, -1 if buffer full
 */
int cayenne_lpp_add_analog(cayenne_lpp_t *lpp, uint8_t channel, float value);

/**
 * @brief Add barometric pressure value
 *
 * @param lpp Encoder context
 * @param channel Channel number (0-255)
 * @param hpa Pressure in hPa (resolution: 0.1 hPa)
 * @return 0 on success, -1 if buffer full
 */
int cayenne_lpp_add_barometric_pressure(cayenne_lpp_t *lpp, uint8_t channel, float hpa);

/**
 * @brief Get the encoded payload buffer
 *
 * @param lpp Encoder context
 * @return Pointer to the payload buffer
 */
const uint8_t *cayenne_lpp_get_buffer(const cayenne_lpp_t *lpp);

/**
 * @brief Get the current payload size
 *
 * @param lpp Encoder context
 * @return Number of bytes in the payload
 */
size_t cayenne_lpp_get_size(const cayenne_lpp_t *lpp);

#ifdef __cplusplus
}
#endif

#endif /* CAYENNE_LPP_H */
