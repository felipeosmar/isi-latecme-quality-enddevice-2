/**
 * @file fuota_manager.h
 * @brief LoRaWAN FUOTA orchestrator (TS004/TS005/TS006 + esp_delta_ota)
 */
#ifndef FUOTA_MANAGER_H
#define FUOTA_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FUOTA_STATE_IDLE = 0,
    FUOTA_STATE_MC_SETUP,       // McGroupSetupReq received, awaiting McClassCSessionReq
    FUOTA_STATE_FRAG_SETUP,     // FragSessionSetupReq received
    FUOTA_STATE_DOWNLOADING,    // Receiving fragments in Class C
    FUOTA_STATE_DOWNLOAD_DONE,  // All fragments received (or FEC-reconstructed)
    FUOTA_STATE_APPLYING,       // Delta patch being applied to inactive slot
    FUOTA_STATE_VERIFIED,       // Hash OK, awaiting DevUpgradeImageReq
    FUOTA_STATE_REBOOTING,      // esp_ota_set_boot_partition called; reboot pending
    FUOTA_STATE_ERROR,
} fuota_state_t;

typedef struct {
    fuota_state_t state;
    uint32_t session_id;
    uint16_t fragments_total;
    uint16_t fragments_received;
    uint8_t  progress_pct;        // 0-100
    char     last_error[64];
} fuota_status_t;

/**
 * @brief Initialize FUOTA subsystem. Call after lorawan_init() and config_init().
 *        Idempotent; returns ESP_OK if already initialized.
 */
esp_err_t fuota_init(void);

/**
 * @brief FreeRTOS task: registers TS004/005/006 packages after join, then idles
 *        as a coordinator between the three callbacks. Create with:
 *        xTaskCreatePinnedToCore(fuota_task, "fuota", 8192, NULL, 4, NULL, 0)
 */
void fuota_task(void *param);

/**
 * @brief Get a copy of the current FUOTA status (thread-safe).
 */
esp_err_t fuota_get_status(fuota_status_t *out);

#ifdef __cplusplus
}
#endif
#endif
