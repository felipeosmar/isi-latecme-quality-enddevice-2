/**
 * @file fuota_manager.c
 * @brief FUOTA state machine — stub; real logic wired in Phases 1-5
 */
#include "fuota_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lorawan_handler.h"
#include "config_manager.h"

static const char *TAG = "FUOTA";
static fuota_status_t s_status;
static SemaphoreHandle_t s_mutex;

esp_err_t fuota_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = FUOTA_STATE_IDLE;
    ESP_LOGI(TAG, "FUOTA manager initialized (fw version: %s)",
             config_get_firmware_version());
    return ESP_OK;
}

esp_err_t fuota_get_status(fuota_status_t *out)
{
    if (!out || !s_mutex) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;
    memcpy(out, &s_status, sizeof(*out));
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void fuota_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "FUOTA task started (awaiting LoRaWAN join)");
    while (!lorawan_is_joined()) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    ESP_LOGI(TAG, "FUOTA task: joined. Package handlers will be registered in Phase 1-3.");
    // Phases 1-3 will register TS005/TS004/TS006 here.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
