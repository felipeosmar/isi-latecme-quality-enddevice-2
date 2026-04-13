#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize config manager and LittleFS
 * @return esp_err_t ESP_OK on success
 */
esp_err_t config_init(void);

/**
 * @brief Deinitialize config manager
 */
void config_deinit(void);

/**
 * @brief Load configuration from file
 * @return esp_err_t ESP_OK on success
 */
esp_err_t config_load(void);

/**
 * @brief Save configuration to file
 * @return esp_err_t ESP_OK on success
 */
esp_err_t config_save(void);

/**
 * @brief Reset configuration to defaults
 */
void config_reset_defaults(void);

/**
 * @brief Reset configuration to defaults and save to flash (thread-safe).
 *        Use this instead of calling config_reset_defaults() + config_save() separately.
 * @return esp_err_t ESP_OK on success
 */
esp_err_t config_factory_reset(void);

// ============================================================================
// WiFi Configuration
// ============================================================================

const char* config_get_wifi_ssid(void);
const char* config_get_wifi_password(void);
bool config_get_wifi_ap_mode(void);
const char* config_get_ap_ssid(void);
const char* config_get_ap_password(void);

void config_set_wifi_ssid(const char *ssid);
void config_set_wifi_password(const char *password);
void config_set_wifi_ap_mode(bool ap_mode);
void config_set_ap_ssid(const char *ssid);
void config_set_ap_password(const char *password);

// ============================================================================
// LoRaWAN Configuration
// ============================================================================

const char* config_get_dev_eui(void);
const char* config_get_join_eui(void);
const char* config_get_app_key(void);
uint8_t config_get_lorawan_port(void);
uint32_t config_get_uplink_interval(void);
uint8_t config_get_sub_band(void);
bool config_get_adr_enabled(void);

void config_set_dev_eui(const char *eui);
void config_set_join_eui(const char *eui);
void config_set_app_key(const char *key);
void config_set_lorawan_port(uint8_t port);
void config_set_uplink_interval(uint32_t seconds);
void config_set_sub_band(uint8_t band);
void config_set_adr_enabled(bool enabled);

// ============================================================================
// Sensor Configuration
// ============================================================================

uint32_t config_get_sensor_interval(void);
float config_get_temp_correction(void);
float config_get_hum_correction(void);
const char* config_get_device_name(void);

void config_set_sensor_interval(uint32_t seconds);
void config_set_temp_correction(float correction);
void config_set_hum_correction(float correction);
void config_set_device_name(const char *name);

bool config_get_thermocouple_enabled(void);
float config_get_thermocouple_max_temp(void);
uint8_t config_get_thermocouple_sck_pin(void);
uint8_t config_get_thermocouple_so_pin(void);
uint8_t config_get_thermocouple_cs_pin(void);

void config_set_thermocouple_enabled(bool enabled);
void config_set_thermocouple_max_temp(float max_temp);
void config_set_thermocouple_sck_pin(uint8_t pin);
void config_set_thermocouple_so_pin(uint8_t pin);
void config_set_thermocouple_cs_pin(uint8_t pin);
float config_get_thermocouple_min_temp(void);
float config_get_thermocouple_correction(void);
void config_set_thermocouple_min_temp(float min_temp);
void config_set_thermocouple_correction(float correction);

// ============================================================================
// Alarm Threshold Configuration
// ============================================================================

bool  config_get_alarm_temp_enabled(void);
float config_get_alarm_temp_low(void);
float config_get_alarm_temp_high(void);
bool  config_get_alarm_hum_enabled(void);
float config_get_alarm_hum_low(void);
float config_get_alarm_hum_high(void);
bool  config_get_alarm_tc_enabled(void);
float config_get_alarm_tc_low(void);
float config_get_alarm_tc_high(void);

void config_set_alarm_temp_enabled(bool enabled);
void config_set_alarm_temp_low(float val);
void config_set_alarm_temp_high(float val);
void config_set_alarm_hum_enabled(bool enabled);
void config_set_alarm_hum_low(float val);
void config_set_alarm_hum_high(float val);
void config_set_alarm_tc_enabled(bool enabled);
void config_set_alarm_tc_low(float val);
void config_set_alarm_tc_high(float val);

// ============================================================================
// Interface Configuration
// ============================================================================

uint8_t config_get_buzzer_volume(void);
void config_set_buzzer_volume(uint8_t volume);

bool config_get_led_enabled(void);
uint8_t config_get_led_brightness(void);
uint32_t config_get_led_blink_interval_ms(void);
const char* config_get_led_color_normal(void);
const char* config_get_led_color_lorawan(void);
const char* config_get_led_color_wifi(void);
const char* config_get_led_color_error(void);

void config_set_led_enabled(bool enabled);
void config_set_led_brightness(uint8_t brightness);
void config_set_led_blink_interval_ms(uint32_t ms);
void config_set_led_color_normal(const char *color);
void config_set_led_color_lorawan(const char *color);
void config_set_led_color_wifi(const char *color);
void config_set_led_color_error(const char *color);

// ============================================================================
// Web Server Configuration
// ============================================================================

const char* config_get_web_username(void);
const char* config_get_web_password(void);
bool config_get_web_auth_enabled(void);

void config_set_web_username(const char *username);
void config_set_web_password(const char *password);
void config_set_web_auth_enabled(bool enabled);

// ============================================================================
// Auto-Update Configuration
// ============================================================================

bool        config_get_auto_update_enabled(void);
const char* config_get_auto_update_branch(void);
const char* config_get_auto_update_firmware_tag(void);
const char* config_get_auto_update_www_tag(void);

void config_set_auto_update_enabled(bool enabled);
void config_set_auto_update_branch(const char *branch);
void config_set_auto_update_firmware_tag(const char *tag);
void config_set_auto_update_www_tag(const char *tag);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H
