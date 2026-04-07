/**
 * @file alarm_manager.h
 * @brief Per-channel threshold alarm manager
 *
 * Evaluates sensor readings against configured thresholds.
 * State machine per channel: IDLE → ACTIVE → ACKNOWLEDGED → IDLE.
 * Drives buzzer siren when any channel is ACTIVE.
 */

#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"
#include "sensor_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Info about the highest-priority active alarm (for OLED display)
 */
typedef struct {
    char  label[24];    // e.g. "TEMP ALTA", "UMID BAIXA"
    float value;        // current sensor reading
    float threshold;    // the violated limit
    bool  is_high;      // true = above high limit, false = below low limit
    int   extra_count;  // number of additional active alarms beyond this one
} alarm_info_t;

/**
 * @brief Initialize alarm manager (creates mutex, resets channels)
 */
esp_err_t alarm_manager_init(void);

/**
 * @brief Evaluate all channels against current sensor data.
 *        Call this after every sensor read (from sensor_task).
 */
void alarm_manager_evaluate(const sensor_data_t *data);

/**
 * @brief Acknowledge all active alarms (silence siren, keep display normal).
 *        Called by button_handler on short press when alarm is active.
 */
void alarm_manager_acknowledge(void);

/**
 * @brief Returns true if any channel is in ACTIVE state (not ACKNOWLEDGED).
 */
bool alarm_manager_is_active(void);

/**
 * @brief Fill *info with the highest-priority active alarm details.
 * @return true if an active alarm exists, false if none.
 */
bool alarm_manager_get_active_info(alarm_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* ALARM_MANAGER_H */
