/**
 * @file clock_sync.h
 * @brief LoRaWAN Application Layer Clock Synchronization (TS003)
 *
 * Sends AppTimeReq on FPort 202 and applies the server's TimeCorrection
 * to the ESP32 system clock via settimeofday(). After sync, time(NULL)
 * returns valid UTC timestamps.
 */

#ifndef CLOCK_SYNC_H
#define CLOCK_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize clock sync module
 *
 * Resets internal state. Must be called before spawning clock_sync_task.
 * May be called before lorawan_init() — callback registration is deferred
 * to the task after join.
 *
 * @return ESP_OK always
 */
esp_err_t clock_sync_init(void);

/**
 * @brief Send an AppTimeReq uplink and wait for AppTimeAns
 *
 * Builds a 5-byte AppTimeReq and calls lorawan_send() on FPort 202.
 * Blocks until lorawan_send() returns. If the server replies within the
 * LoRaWAN RX windows, the AppTimeAns callback applies settimeofday()
 * before this function returns.
 *
 * Must be called after lorawan_is_joined() == true.
 *
 * @return ESP_OK if uplink was sent (regardless of whether sync succeeded),
 *         ESP_FAIL on lorawan_send() error
 */
esp_err_t clock_sync_request(void);

/**
 * @brief Check if the clock has been synchronized at least once
 *
 * @return true after a successful AppTimeAns has been applied
 */
bool clock_sync_is_synced(void);

/**
 * @brief Get current Unix timestamp
 *
 * @return time(NULL) if synced, 0 if not yet synced
 */
time_t clock_sync_get_time(void);

/**
 * @brief FreeRTOS task: syncs on boot, re-syncs every 24 hours
 *
 * Waits for LoRaWAN join, registers the TS003 downlink callback via
 * lorawan_add_app_package(), sends the initial AppTimeReq, then loops
 * with a 24-hour delay for daily re-sync.
 *
 * Create with: xTaskCreatePinnedToCore(clock_sync_task, "clock_sync",
 *                                      3072, NULL, 3, NULL, 0)
 */
void clock_sync_task(void *param);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_SYNC_H */
