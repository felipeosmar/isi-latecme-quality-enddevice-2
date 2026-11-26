/**
 * @file alarm_events.h
 * @brief Coalesced signaling for platform alarm event uplinks (fPort 3).
 *
 * Producers (alarm_manager, limits apply/reject) call notify/button/set_error.
 * Any number of notifications collapse into one pending event; the sender
 * builds a single snapshot of the final state.
 *
 * Sender protocol (Task 4, uplink_task):
 *   if (alarm_events_wait(timeout)) {
 *       uint8_t b, e;
 *       alarm_events_take(&b, &e);        // clears EVT_PENDING first
 *       ...build and send event...
 *       if (sent ok)  alarm_events_commit(b, e);
 *       else          alarm_events_requeue();
 *   }
 * A notification raised while the send blocks (RX windows) re-sets the bit
 * after take() and therefore produces another event; commit() never clears it.
 *
 * All functions are safe to call before alarm_events_init(): the event-group
 * operations become no-ops (wait() sleeps for its timeout; counters still
 * accumulate), and init starts with
 * EVT_PENDING set anyway.
 */

#ifndef ALARM_EVENTS_H
#define ALARM_EVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create the event group. Starts with EVT_PENDING set (first event after join). */
void alarm_events_init(void);

/** Mark an event as pending. */
void alarm_events_notify(void);

/** Count one silencing button press (saturates at 255) and notify. */
void alarm_events_button(void);

/** Store the last limits error code (0 = OK) and notify. */
void alarm_events_set_error(uint8_t code);

/**
 * Wait for EVT_PENDING without clearing it.
 * @return true if the bit was set; false on timeout. If not initialized,
 *         sleeps for timeout and returns false (no busy loop).
 */
bool alarm_events_wait(TickType_t timeout);

/**
 * Clear EVT_PENDING, then read the pending button count and error code
 * (without zeroing them). Either pointer may be NULL.
 */
void alarm_events_take(uint8_t *button, uint8_t *error);

/** Sending failed or no join: set EVT_PENDING again. */
void alarm_events_requeue(void);

/**
 * Call only after lorawan_send returned ESP_OK with the values from take().
 * Subtracts button_sent (floor 0); clears the error only if it still equals
 * error_sent. If anything is still pending, sets EVT_PENDING. Never clears it.
 */
void alarm_events_commit(uint8_t button_sent, uint8_t error_sent);

#ifdef __cplusplus
}
#endif

#endif /* ALARM_EVENTS_H */
