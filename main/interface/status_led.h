#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "esp_err.h"
#include "health_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STATUS_LED_GPIO 2

typedef enum {
    LED_STATE_NORMAL = 0,
    LED_STATE_LORAWAN_NOT_CONFIGURED,
    LED_STATE_WIFI_DISCONNECTED,
    LED_STATE_ERROR,
} led_state_t;

/**
 * @brief Initialize WS2812 LED on GPIO2
 * @return ESP_OK on success
 */
esp_err_t status_led_init(void);

/**
 * @brief Evaluate system health and blink LED accordingly.
 *        Call this periodically from health monitor task.
 * @param health Current system health snapshot
 */
void status_led_update(const system_health_t *health);

/**
 * @brief Deinitialize LED driver
 */
void status_led_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // STATUS_LED_H
