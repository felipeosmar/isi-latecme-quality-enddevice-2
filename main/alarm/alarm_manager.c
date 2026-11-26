/**
 * @file alarm_manager.c
 * @brief Alarm state machine with per-channel evaluation and buzzer siren task
 *
 * Channel index mapping:
 *   0 = I2C temperature
 *   1 = I2C humidity
 *   2 = thermocouple temperature
 *
 * Priority order for display (highest first): 2 (tc) > 0 (temp) > 1 (hum)
 */

#include "alarm_manager.h"
#include "alarm_events.h"
#include "config_manager.h"
#include "buzzer.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "ALARM";

// ============================================================================
// Internal types
// ============================================================================

typedef enum {
    ALARM_STATE_IDLE = 0,
    ALARM_STATE_ACTIVE,
    ALARM_STATE_ACKNOWLEDGED,
} alarm_state_t;

typedef struct {
    alarm_state_t state;
    float         value;        // last evaluated reading
    float         threshold;    // violated limit
    bool          is_high;      // true = above high, false = below low
    char          label[24];    // "TEMP ALTA", etc.
} alarm_channel_t;

static alarm_channel_t s_channels[3];
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_siren_task = NULL;
static bool s_initialized = false;

// ============================================================================
// Internal: siren task
// ============================================================================

static void alarm_siren_task(void *param)
{
    while (1) {
        buzzer_beep_pattern(3, 100, 100);   // 3 rapid beeps
        vTaskDelay(pdMS_TO_TICKS(2000));    // 2s pause
    }
}

// ============================================================================
// Internal: evaluate a single channel (called with mutex held)
// ============================================================================

static void evaluate_channel(alarm_channel_t *ch, float value,
                              float low, float high,
                              const char *label_high, const char *label_low)
{
    // Inverted or unconfigured thresholds: skip evaluation to prevent un-clearable alarms
    if (low >= high) return;

    bool violates_high = (value > high);
    bool violates_low  = (value < low);
    bool in_range      = (!violates_high && !violates_low);

    switch (ch->state) {
        case ALARM_STATE_IDLE:
            if (violates_high) {
                ch->state     = ALARM_STATE_ACTIVE;
                ch->value     = value;
                ch->threshold = high;
                ch->is_high   = true;
                strncpy(ch->label, label_high, sizeof(ch->label) - 1);
                ch->label[sizeof(ch->label) - 1] = '\0';
                ESP_LOGW(TAG, "Alarm ACTIVE: %s (%.1f > %.1f)", ch->label, value, high);
            } else if (violates_low) {
                ch->state     = ALARM_STATE_ACTIVE;
                ch->value     = value;
                ch->threshold = low;
                ch->is_high   = false;
                strncpy(ch->label, label_low, sizeof(ch->label) - 1);
                ch->label[sizeof(ch->label) - 1] = '\0';
                ESP_LOGW(TAG, "Alarm ACTIVE: %s (%.1f < %.1f)", ch->label, value, low);
            }
            break;

        case ALARM_STATE_ACTIVE:
            ch->value = value;  // keep display current
            break;

        case ALARM_STATE_ACKNOWLEDGED:
            if (in_range) {
                ch->state = ALARM_STATE_IDLE;
                ESP_LOGI(TAG, "Alarm re-armed: %s back in range", ch->label);
            }
            break;
    }
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t alarm_manager_init(void)
{
    if (s_initialized) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(s_channels, 0, sizeof(s_channels));
    s_siren_task  = NULL;
    s_initialized = true;
    ESP_LOGI(TAG, "Alarm manager initialized (3 channels)");
    return ESP_OK;
}

void alarm_manager_evaluate(const sensor_data_t *data)
{
    if (!s_initialized || !data) return;

    // Single locked read: avoids seeing a new low with an old high.
    // Taken before s_mutex so the two locks are never nested.
    alarm_channel_cfg_t cfg[3];
    config_get_alarm_block(cfg);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    alarm_state_t before[3] = {
        s_channels[0].state, s_channels[1].state, s_channels[2].state,
    };

    // Channel 0: I2C temperature
    if (data->temp_hum_valid && cfg[0].enabled) {
        evaluate_channel(&s_channels[0], data->temperature,
                         cfg[0].low, cfg[0].high,
                         "TEMP ALTA", "TEMP BAIXA");
    } else if (!cfg[0].enabled) {
        s_channels[0].state = ALARM_STATE_IDLE;
    }
    // if enabled but data invalid: leave state unchanged

    // Channel 1: I2C humidity
    if (data->temp_hum_valid && cfg[1].enabled) {
        evaluate_channel(&s_channels[1], data->humidity,
                         cfg[1].low, cfg[1].high,
                         "UMID ALTA", "UMID BAIXA");
    } else if (!cfg[1].enabled) {
        s_channels[1].state = ALARM_STATE_IDLE;
    }

    // Channel 2: thermocouple
    if (data->thermocouple_valid && cfg[2].enabled) {
        evaluate_channel(&s_channels[2], data->thermocouple_temp,
                         cfg[2].low, cfg[2].high,
                         "TERM ALTA", "TERM BAIXA");
    } else if (!cfg[2].enabled) {
        s_channels[2].state = ALARM_STATE_IDLE;
    }

    // Start or stop siren task based on active channels
    bool any_active = (s_channels[0].state == ALARM_STATE_ACTIVE ||
                       s_channels[1].state == ALARM_STATE_ACTIVE ||
                       s_channels[2].state == ALARM_STATE_ACTIVE);

    if (any_active && s_siren_task == NULL) {
        BaseType_t ret = xTaskCreatePinnedToCore(alarm_siren_task, "siren", 2048, NULL, 3, &s_siren_task, 0);
        if (ret == pdPASS) {
            ESP_LOGI(TAG, "Siren started");
        } else {
            s_siren_task = NULL;
            ESP_LOGE(TAG, "Failed to create siren task (heap exhausted?)");
        }
    } else if (!any_active && s_siren_task != NULL) {
        vTaskDelete(s_siren_task);
        s_siren_task = NULL;
        buzzer_off();
        ESP_LOGI(TAG, "Siren stopped (all channels cleared)");
    }

    // Includes transitions forced to IDLE by a disabled channel
    bool changed = false;
    for (int i = 0; i < 3; i++) {
        if (s_channels[i].state != before[i]) changed = true;
    }

    xSemaphoreGive(s_mutex);

    if (changed) alarm_events_notify();
}

void alarm_manager_acknowledge(void)
{
    if (!s_initialized) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    bool silenced = false;
    for (int i = 0; i < 3; i++) {
        if (s_channels[i].state == ALARM_STATE_ACTIVE) {
            s_channels[i].state = ALARM_STATE_ACKNOWLEDGED;
            silenced = true;
            ESP_LOGI(TAG, "Channel %d acknowledged", i);
        }
    }

    if (s_siren_task != NULL) {
        vTaskDelete(s_siren_task);
        s_siren_task = NULL;
        buzzer_off();
        ESP_LOGI(TAG, "Siren silenced by acknowledge");
    }

    xSemaphoreGive(s_mutex);

    // One press counts once, however many channels it silenced
    if (silenced) alarm_events_button();
}

bool alarm_manager_is_active(void)
{
    if (!s_initialized) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool active = (s_channels[0].state == ALARM_STATE_ACTIVE ||
                   s_channels[1].state == ALARM_STATE_ACTIVE ||
                   s_channels[2].state == ALARM_STATE_ACTIVE);
    xSemaphoreGive(s_mutex);
    return active;
}

uint8_t alarm_manager_get_channel_state(int ch)
{
    if (!s_initialized || ch < 0 || ch > 2) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint8_t state = (uint8_t)s_channels[ch].state;
    xSemaphoreGive(s_mutex);
    return state;
}

bool alarm_manager_get_active_info(alarm_info_t *info)
{
    if (!s_initialized || !info) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // Priority: thermocouple (2) > temperature (0) > humidity (1)
    const int priority[3] = {2, 0, 1};
    int found = -1;
    int count = 0;

    for (int p = 0; p < 3; p++) {
        int i = priority[p];
        if (s_channels[i].state == ALARM_STATE_ACTIVE) {
            count++;
            if (found == -1) found = i;
        }
    }

    if (found == -1) {
        xSemaphoreGive(s_mutex);
        return false;
    }

    strncpy(info->label, s_channels[found].label, sizeof(info->label) - 1);
    info->label[sizeof(info->label) - 1] = '\0';
    info->value       = s_channels[found].value;
    info->threshold   = s_channels[found].threshold;
    info->is_high     = s_channels[found].is_high;
    info->extra_count = count - 1;

    xSemaphoreGive(s_mutex);
    return true;
}
