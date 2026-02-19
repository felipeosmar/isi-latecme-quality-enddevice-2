/**
 * @file buzzer.h
 * @brief Buzzer driver for audible feedback
 *
 * GPIO4 drives a BC817-25 transistor to activate the buzzer.
 * Active HIGH = buzzer ON.
 */

#ifndef BUZZER_H
#define BUZZER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Buzzer GPIO (schematic: GPIO4 → R5 1k → BC817-25 → Buzzer)
#define BUZZER_GPIO     4

/**
 * @brief Initialize buzzer GPIO as output
 * @return ESP_OK on success
 */
esp_err_t buzzer_init(void);

/**
 * @brief Play a tone at specific frequency
 * @param freq_hz Frequency in Hz (e.g. 2700)
 * @param duration_ms Tone duration in milliseconds
 */
void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms);

/**
 * @brief Single short beep at default frequency (2700 Hz)
 * @param duration_ms Beep duration in milliseconds
 */
void buzzer_beep(uint32_t duration_ms);

/**
 * @brief Multiple beeps with pause between them
 * @param count Number of beeps
 * @param on_ms Beep ON duration in milliseconds
 * @param off_ms Pause between beeps in milliseconds
 */
void buzzer_beep_pattern(uint8_t count, uint32_t on_ms, uint32_t off_ms);

/**
 * @brief Turn buzzer ON (manual control)
 */
void buzzer_on(void);

/**
 * @brief Turn buzzer OFF (manual control)
 */
void buzzer_off(void);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_H */
