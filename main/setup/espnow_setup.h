/**
 * @file espnow_setup.h
 * @brief ESP-NOW factory setup-mode responder
 *
 * Runs a standalone ESP-NOW protocol responder used during factory
 * provisioning. A PC-side app (via a USB "dongle" running the sibling
 * ESP-NOW/serial bridge firmware) discovers the device, reads live sensor
 * data, applies calibration corrections, and commits identity/config.
 */

#ifndef ESPNOW_SETUP_H
#define ESPNOW_SETUP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter factory setup mode.
 *
 * Initializes WiFi STA + ESP-NOW on channel 1, registers the receive
 * callback, and loops forever handling DISCOVER/READ/SET/IDENTIFY/COMMIT
 * requests. Never returns (loops until a COMMIT triggers a reboot).
 */
void espnow_setup_run(void);

#ifdef __cplusplus
}
#endif

#endif /* ESPNOW_SETUP_H */
