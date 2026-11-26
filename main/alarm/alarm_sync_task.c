/**
 * @file alarm_sync_task.c
 * @brief Consumes fPort 11 downlinks queued by lorawan_send: parse, validate,
 *        apply and report the result via the fPort 3 event (error code).
 */
#include "alarm_sync_task.h"
#include "alarm_sync.h"
#include "alarm_events.h"
#include "config_manager.h"
#include "lorawan_handler.h"

#include <stdint.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "alarm_sync";

typedef struct {
    uint8_t len;
    uint8_t data[ALARM_SYNC_LIMITS_LEN];
} limits_msg_t;

static void alarm_sync_task(void *param)
{
    (void)param;

    // The queue is created by lorawan_init(), which runs in lorawan_task.
    QueueHandle_t q;
    while ((q = lorawan_limits_queue()) == NULL) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    limits_msg_t msg;
    for (;;) {
        if (xQueueReceive(q, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        alarm_sync_limits_t l;
        uint8_t err;

        if (msg.len != ALARM_SYNC_LIMITS_LEN) {
            err = ALARM_SYNC_ERR_FORMAT;
        } else {
            err = (uint8_t)alarm_sync_parse_limits(msg.data, msg.len, &l);
        }
        if (err == ALARM_SYNC_OK) {
#if !CONFIG_THERMOCOUPLE_ENABLED
            l.ch[2].keep = true;   // no thermocouple on this SKU: never validate/apply it
#endif
            err = (uint8_t)alarm_sync_validate(&l);
        }
        if (err == ALARM_SYNC_OK) {
            if (config_apply_alarm_limits(&l, false) != ESP_OK) {
                err = ALARM_SYNC_ERR_SAVE;
            }
        }

        if (err == ALARM_SYNC_OK) {
            ESP_LOGI(TAG, "applied v%u", (unsigned)l.version);
        } else {
            ESP_LOGW(TAG, "rejected err=%u", (unsigned)err);
        }
        // 0 also counts: it triggers the confirmation event with the new version.
        alarm_events_set_error(err);

        ESP_LOGI(TAG, "alarm_sync stack high water mark: %u bytes",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

void alarm_sync_task_start(void)
{
    xTaskCreatePinnedToCore(alarm_sync_task, "alarm_sync", 4096, NULL, 3, NULL, 0);
}
