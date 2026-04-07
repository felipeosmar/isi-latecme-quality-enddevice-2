/**
 * @file oled_display.h
 * @brief SSD1306 128x64 OLED display driver over I2C
 *
 * I2C Address: 0x3C
 * Shares I2C bus with sensors (GPIO21/GPIO22).
 * Two pages: Sensors (split view) and System (with LoRaWAN status).
 */

#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLED_I2C_ADDR   0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

/**
 * @brief Display page enum
 */
typedef enum {
    OLED_PAGE_SENSORS = 0,
    OLED_PAGE_SYSTEM,
    OLED_PAGE_MAX
} oled_page_t;

/**
 * @brief Initialize OLED display
 */
esp_err_t oled_display_init(void *i2c_bus);

/**
 * @brief Clear the display
 */
void oled_display_clear(void);

/**
 * @brief Write text at position
 */
void oled_display_text(uint8_t x, uint8_t y, const char *text);

/**
 * @brief Update the display buffer to screen
 */
void oled_display_update(void);

/**
 * @brief Show sensor data page (split layout)
 *
 * Left half: T and H from I2C sensor
 * Right half: T from thermocouple
 */
void oled_display_show_sensors(float temp, float hum, float tc_temp);

/**
 * @brief Show system info + LoRaWAN status page
 */
void oled_display_show_system(const char *ip_addr, uint32_t uptime_s, uint32_t free_heap,
                               bool lora_joined, uint32_t dev_addr,
                               uint32_t uplink_count, int16_t rssi, float snr);

/**
 * @brief Cycle to the next display page
 */
void oled_display_next_page(void);

/**
 * @brief Get current page
 */
oled_page_t oled_display_get_page(void);

#ifdef __cplusplus
}
#endif

#endif /* OLED_DISPLAY_H */
