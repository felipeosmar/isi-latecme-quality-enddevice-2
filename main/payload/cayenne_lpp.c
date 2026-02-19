/**
 * @file cayenne_lpp.c
 * @brief CayenneLPP payload encoder implementation
 *
 * CayenneLPP format per field:
 *   [1 byte channel] [1 byte type] [N bytes data]
 *
 * Data encoding:
 *   Temperature: 2 bytes, signed, 0.1°C resolution
 *   Humidity:    1 byte, unsigned, 0.5% resolution
 *   Analog:      2 bytes, signed, 0.01 resolution
 *   Pressure:    2 bytes, unsigned, 0.1 hPa resolution
 */

#include "cayenne_lpp.h"
#include <string.h>

void cayenne_lpp_reset(cayenne_lpp_t *lpp)
{
    memset(lpp->buffer, 0, sizeof(lpp->buffer));
    lpp->cursor = 0;
}

int cayenne_lpp_add_temperature(cayenne_lpp_t *lpp, uint8_t channel, float celsius)
{
    // Need 4 bytes: channel + type + 2 data bytes
    if (lpp->cursor + 4 > LPP_MAX_SIZE) {
        return -1;
    }

    int16_t val = (int16_t)(celsius * 10.0f);

    lpp->buffer[lpp->cursor++] = channel;
    lpp->buffer[lpp->cursor++] = LPP_TEMPERATURE;
    lpp->buffer[lpp->cursor++] = (uint8_t)(val >> 8);
    lpp->buffer[lpp->cursor++] = (uint8_t)(val & 0xFF);

    return 0;
}

int cayenne_lpp_add_humidity(cayenne_lpp_t *lpp, uint8_t channel, float percent)
{
    // Need 3 bytes: channel + type + 1 data byte
    if (lpp->cursor + 3 > LPP_MAX_SIZE) {
        return -1;
    }

    uint8_t val = (uint8_t)(percent * 2.0f);

    lpp->buffer[lpp->cursor++] = channel;
    lpp->buffer[lpp->cursor++] = LPP_RELATIVE_HUMIDITY;
    lpp->buffer[lpp->cursor++] = val;

    return 0;
}

int cayenne_lpp_add_analog(cayenne_lpp_t *lpp, uint8_t channel, float value)
{
    // Need 4 bytes: channel + type + 2 data bytes
    if (lpp->cursor + 4 > LPP_MAX_SIZE) {
        return -1;
    }

    int16_t val = (int16_t)(value * 100.0f);

    lpp->buffer[lpp->cursor++] = channel;
    lpp->buffer[lpp->cursor++] = LPP_ANALOG_INPUT;
    lpp->buffer[lpp->cursor++] = (uint8_t)(val >> 8);
    lpp->buffer[lpp->cursor++] = (uint8_t)(val & 0xFF);

    return 0;
}

int cayenne_lpp_add_barometric_pressure(cayenne_lpp_t *lpp, uint8_t channel, float hpa)
{
    // Need 4 bytes: channel + type + 2 data bytes
    if (lpp->cursor + 4 > LPP_MAX_SIZE) {
        return -1;
    }

    uint16_t val = (uint16_t)(hpa * 10.0f);

    lpp->buffer[lpp->cursor++] = channel;
    lpp->buffer[lpp->cursor++] = LPP_BAROMETRIC_PRESSURE;
    lpp->buffer[lpp->cursor++] = (uint8_t)(val >> 8);
    lpp->buffer[lpp->cursor++] = (uint8_t)(val & 0xFF);

    return 0;
}

const uint8_t *cayenne_lpp_get_buffer(const cayenne_lpp_t *lpp)
{
    return lpp->buffer;
}

size_t cayenne_lpp_get_size(const cayenne_lpp_t *lpp)
{
    return lpp->cursor;
}
