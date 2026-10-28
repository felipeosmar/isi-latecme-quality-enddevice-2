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
#include <stddef.h>
#include "esp_err.h"
#include "alarm_manager.h"
#include "sensor_manager.h"

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

#if CONFIG_OLED_ENABLED

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
void oled_display_show_sensors(float temp, float hum, float tc_temp,
                               const char *status_top, const char *status_bottom);

/**
 * @brief Show system info + LoRaWAN status page
 */
void oled_display_show_system(const char *ip_addr, uint32_t uptime_s,
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

/**
 * @brief Show factory reset progress screen.
 *        Call repeatedly during button long press to give visual feedback.
 * @param percent 0-100 progress towards triggering the reset
 */
void oled_display_show_factory_reset(uint8_t percent);

/**
 * @brief Show the factory setup-mode screen.
 *
 * Line 1 (2x font): "SETUP"
 * Line 2: own STA MAC address
 * Line 3: setup state (inverted)
 * Lines 4+: one row per sensor with the reading next to its applied offset
 *
 * @param mac_str   MAC address as "AA:BB:CC:DD:EE:FF"
 * @param data      Latest readings (corrections already applied); NULL shows "--"
 * @param temp_corr I2C temperature correction offset (degrees C)
 * @param hum_corr  I2C humidity correction offset (%)
 * @param tc_corr   Thermocouple correction offset (degrees C)
 * @param state     Setup state line, drawn inverted on page 3; NULL leaves it blank
 */
void oled_display_show_setup(const char *mac_str, const sensor_data_t *data,
                             float temp_corr, float hum_corr, float tc_corr,
                             const char *state);

/**
 * @brief Show alarm page with 1Hz blinking header.
 *        Toggles display inversion each call (call at 500ms interval for 1Hz blink).
 *        Resets inversion to normal automatically when alarm clears.
 *
 * @param info Active alarm details from alarm_manager_get_active_info()
 */
void oled_display_show_alarm(const alarm_info_t *info);

/**
 * @brief True once the OLED was probed and initialized
 */
bool oled_display_is_ready(void);

/**
 * @brief Show the boot list screen.
 *
 * @param header  Text drawn in inverted video on page 0
 * @param lines   Status lines for pages 1-7; NULL entries are skipped
 * @param n_lines Number of entries in @p lines (only the first 7 are drawn)
 */
void oled_display_show_boot(const char *header, const char *const *lines, size_t n_lines);

#else // CONFIG_OLED_ENABLED

// Stub implementations when OLED is disabled
static inline esp_err_t oled_display_init(void *i2c_bus) { return ESP_OK; }
static inline void oled_display_clear(void) { }
static inline void oled_display_text(uint8_t x, uint8_t y, const char *text) { }
static inline void oled_display_update(void) { }
static inline void oled_display_show_sensors(float temp, float hum, float tc_temp,
                                             const char *status_top, const char *status_bottom) { }
static inline void oled_display_show_system(const char *ip_addr, uint32_t uptime_s,
                                            bool lora_joined, uint32_t dev_addr,
                                            uint32_t uplink_count, int16_t rssi, float snr) { }
static inline void oled_display_next_page(void) { }
static inline oled_page_t oled_display_get_page(void) { return OLED_PAGE_SENSORS; }
static inline void oled_display_show_factory_reset(uint8_t percent) { }
static inline void oled_display_show_setup(const char *mac_str,
                                           const sensor_data_t *data,
                                           float temp_corr, float hum_corr,
                                           float tc_corr, const char *state) { }
static inline void oled_display_show_alarm(const alarm_info_t *info) { }
static inline bool oled_display_is_ready(void) { return false; }
static inline void oled_display_show_boot(const char *header, const char *const *lines, size_t n_lines) { }

#endif // CONFIG_OLED_ENABLED

#ifdef __cplusplus
}
#endif

#endif /* OLED_DISPLAY_H */
