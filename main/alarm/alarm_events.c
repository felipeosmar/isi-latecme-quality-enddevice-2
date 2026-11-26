/**
 * @file alarm_events.c
 * @brief Coalesced signaling for platform alarm event uplinks (fPort 3).
 *
 * Ordering rules (see alarm_events.h):
 *  - take() clears EVT_PENDING *before* reading the counters, so anything
 *    notified afterwards re-sets the bit and yields another event.
 *  - commit() only ever sets the bit (when something newer is still pending).
 *  - Event-group calls are never made inside the portMUX critical section.
 */

#include "alarm_events.h"

#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "ALARM_EVT";

#define EVT_PENDING BIT0

static EventGroupHandle_t s_group = NULL;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_button = 0;   // silencing presses since last sent event
static uint8_t s_error  = 0;   // last limits error code (0 = OK)

void alarm_events_init(void)
{
    if (s_group) return;

    s_group = xEventGroupCreate();
    if (!s_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }
    // First event right after join: platform learns IP/version after reboot/update.
    xEventGroupSetBits(s_group, EVT_PENDING);
}

void alarm_events_notify(void)
{
    if (s_group) xEventGroupSetBits(s_group, EVT_PENDING);
}

void alarm_events_button(void)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_button < 255) s_button++;
    taskEXIT_CRITICAL(&s_lock);
    alarm_events_notify();
}

void alarm_events_set_error(uint8_t code)
{
    taskENTER_CRITICAL(&s_lock);
    s_error = code;
    taskEXIT_CRITICAL(&s_lock);
    alarm_events_notify();
}

bool alarm_events_wait(TickType_t timeout)
{
    if (!s_group) {
        // Never initialized (e.g. out of heap): keep timeout semantics so a
        // loop caller blocks instead of spinning hot.
        vTaskDelay(timeout);
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_group, EVT_PENDING,
                                           pdFALSE,  /* do not clear */
                                           pdFALSE, timeout);
    return (bits & EVT_PENDING) != 0;
}

void alarm_events_take(uint8_t *button, uint8_t *error)
{
    // Clear first: a notify() after this point re-sets the bit.
    if (s_group) xEventGroupClearBits(s_group, EVT_PENDING);

    taskENTER_CRITICAL(&s_lock);
    uint8_t b = s_button;
    uint8_t e = s_error;
    taskEXIT_CRITICAL(&s_lock);

    if (button) *button = b;
    if (error)  *error  = e;
}

void alarm_events_requeue(void)
{
    alarm_events_notify();
}

void alarm_events_commit(uint8_t button_sent, uint8_t error_sent)
{
    taskENTER_CRITICAL(&s_lock);
    s_button = (s_button > button_sent) ? (uint8_t)(s_button - button_sent) : 0;
    if (s_error == error_sent) s_error = 0;   // a newer error survives
    bool still_pending = (s_button > 0 || s_error != 0);
    taskEXIT_CRITICAL(&s_lock);

    if (still_pending) alarm_events_notify();
}
