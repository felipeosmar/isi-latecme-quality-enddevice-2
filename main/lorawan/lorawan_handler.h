/**
 * @file lorawan_handler.h
 * @brief LoRaWAN handler for OTAA activation and uplink/downlink
 *
 * Uses RadioLib with SX1276/SX1278 for LoRaWAN Class A communication.
 * Supports AU915 frequency plan with sub-band selection.
 */

#ifndef LORAWAN_HANDLER_H
#define LORAWAN_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief LoRaWAN statistics
 */
typedef struct {
    bool joined;
    uint32_t uplink_count;
    uint32_t downlink_count;
    int16_t last_rssi;
    float last_snr;
    uint8_t data_rate;
    uint32_t dev_addr;
    uint32_t last_uplink_ms;
    uint32_t last_join_attempt_ms;
    uint8_t join_attempts;
} lorawan_stats_t;

/**
 * @brief Initialize the LoRaWAN handler
 *
 * Sets up SPI, configures the SX127x radio, and prepares
 * for OTAA join. Must be called before any other lorawan function.
 *
 * @return ESP_OK on success
 */
esp_err_t lorawan_init(void);

/**
 * @brief Attempt OTAA join
 *
 * Reads DevEUI, JoinEUI, and AppKey from config manager.
 * Blocks until join succeeds or times out.
 *
 * @return ESP_OK if joined, ESP_ERR_TIMEOUT if join failed
 */
esp_err_t lorawan_join(void);

/**
 * @brief Send uplink data
 *
 * @param data Payload buffer
 * @param len Payload length (max 51 bytes for DR0)
 * @param port LoRaWAN FPort (1-223)
 * @param confirmed true for confirmed uplink, false for unconfirmed
 * @return ESP_OK if sent successfully
 */
esp_err_t lorawan_send(const uint8_t *data, size_t len, uint8_t port, bool confirmed);

/**
 * @brief Check if device is joined to the network
 *
 * @return true if joined
 */
bool lorawan_is_joined(void);

/**
 * @brief Get LoRaWAN statistics
 *
 * @param stats Pointer to stats structure to fill
 * @return ESP_OK on success
 */
esp_err_t lorawan_get_stats(lorawan_stats_t *stats);

/**
 * @brief Force a re-join
 *
 * Clears current session and attempts a new OTAA join.
 *
 * @return ESP_OK if join succeeded
 */
esp_err_t lorawan_force_rejoin(void);

/**
 * @brief LoRaWAN FreeRTOS task
 *
 * Main task that handles periodic uplinks and manages the LoRaWAN session.
 * Should be created with xTaskCreatePinnedToCore on Core 1.
 *
 * @param param Unused
 */
void lorawan_task(void *param);

#ifdef __cplusplus
}
#endif

#endif /* LORAWAN_HANDLER_H */
