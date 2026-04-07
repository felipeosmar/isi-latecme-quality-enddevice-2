/**
 * @file button_handler.h
 * @brief Physical button handler for GPIO25
 *
 * Short press (<2s):
 *   - If alarm active: alarm_manager_acknowledge()
 *   - Else: oled_display_next_page()
 *
 * Long press (≥30s): factory reset (config_reset_defaults + restart)
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_GPIO     25

/**
 * @brief Initialize button handler: configure GPIO25, install ISR, create task.
 * @return ESP_OK on success
 */
esp_err_t button_handler_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_HANDLER_H */
