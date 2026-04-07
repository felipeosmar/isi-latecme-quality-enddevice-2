/**
 * @file auto_updater.h
 * @brief GitHub automatic OTA update checker
 *
 * Checks once per day for a new GitHub release on the configured branch.
 * If a newer release is found (higher sequential N in tag `{branch}-rN`),
 * downloads and flashes firmware first, then www on the next cycle.
 * Only runs when WiFi is connected in STA mode.
 */

#ifndef AUTO_UPDATER_H
#define AUTO_UPDATER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUTO_UPDATE_RESULT_NEVER,       // Never checked
    AUTO_UPDATE_RESULT_UP_TO_DATE,  // Check ran, nothing to do
    AUTO_UPDATE_RESULT_UPDATED,     // OTA applied, rebooting
    AUTO_UPDATE_RESULT_ERROR,       // Check or OTA failed
} auto_update_result_t;

/**
 * @brief Initialize and start the auto-updater background task.
 *        Call once after WiFi init and web server init.
 * @return ESP_OK on success
 */
esp_err_t auto_updater_init(void);

/**
 * @brief Get the result of the last update check.
 */
auto_update_result_t auto_updater_get_last_result(void);

/**
 * @brief Get Unix timestamp of last check (0 if never checked).
 */
int64_t auto_updater_get_last_check_time(void);

/**
 * @brief Signal the task to run a check as soon as possible (non-blocking).
 *        If a check is currently running, the next check starts immediately after.
 */
void auto_updater_trigger_now(void);

#ifdef __cplusplus
}
#endif

#endif // AUTO_UPDATER_H
