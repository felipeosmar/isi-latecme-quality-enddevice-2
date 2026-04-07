# Alarm & Button Handler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-sensor threshold alarms (buzzer siren + OLED alarm page) and a physical button (GPIO25) for alarm acknowledge, page navigation, and factory reset.

**Architecture:** Two new modules (`alarm_manager`, `button_handler`) plus targeted changes to `config_manager`, `oled_display`, `sensor_manager`, `main.c`, the web API, and the web UI. Each module has a single responsibility and communicates through well-defined public APIs.

**Tech Stack:** ESP-IDF v5.5.3, FreeRTOS, C99, cJSON, SSD1306 OLED (I2C), passive buzzer (LEDC PWM GPIO4), button (GPIO25 with internal pull-up).

**Build command** (run from project root, no test suite exists):
```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build
```
Expected: `Project build complete. To flash...` with no errors.

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `main/alarm/alarm_manager.h` | Create | Public API: types, function declarations |
| `main/alarm/alarm_manager.c` | Create | State machine, siren task, thread-safe evaluation |
| `main/interface/button_handler.h` | Create | Public API |
| `main/interface/button_handler.c` | Create | GPIO ISR, press classification, dispatch |
| `main/config/config_manager.h` | Modify | Declare 9 alarm threshold getters/setters |
| `main/config/config_manager.c` | Modify | Struct fields, defaults, JSON save/load, implementations |
| `main/display/oled_display.h` | Modify | Declare `oled_display_show_alarm()` |
| `main/display/oled_display.c` | Modify | Implement alarm page with 1Hz blink; reset invert in normal pages |
| `main/sensors/sensor_manager.c` | Modify | Call `alarm_manager_evaluate()` after each read |
| `main/main.c` | Modify | Add init calls, remove display auto-cycle, update display_task |
| `main/CMakeLists.txt` | Modify | Add new source files and include dirs |
| `main/webserver/handlers/api_sensors.c` | Modify | Expose alarm thresholds in GET/POST |
| `main/www/tabs/config.html` | Modify | Add Alarmes section |
| `main/www/tabs/config.js` | Modify | Load/save alarm config |

---

## Task 1: config_manager — Alarm Threshold Fields

**Files:**
- Modify: `main/config/config_manager.h`
- Modify: `main/config/config_manager.c`

- [ ] **Step 1: Add alarm fields to `config_t` struct**

In `main/config/config_manager.c`, after the `bool web_auth_enabled;` line (end of `config_t` struct, before the closing `}`), add:

```c
    // Alarm thresholds
    bool  alarm_temp_enabled;
    float alarm_temp_low;
    float alarm_temp_high;
    bool  alarm_hum_enabled;
    float alarm_hum_low;
    float alarm_hum_high;
    bool  alarm_tc_enabled;
    float alarm_tc_low;
    float alarm_tc_high;
```

- [ ] **Step 2: Add defaults in `config_reset_defaults()`**

In `config_manager.c`, at the end of `config_reset_defaults()`, before the `ESP_LOGI` line, add:

```c
    // Alarm thresholds (all disabled by default)
    s_config.alarm_temp_enabled = false;
    s_config.alarm_temp_low     = 0.0f;
    s_config.alarm_temp_high    = 0.0f;
    s_config.alarm_hum_enabled  = false;
    s_config.alarm_hum_low      = 0.0f;
    s_config.alarm_hum_high     = 0.0f;
    s_config.alarm_tc_enabled   = false;
    s_config.alarm_tc_low       = 0.0f;
    s_config.alarm_tc_high      = 0.0f;
```

- [ ] **Step 3: Add JSON save in `_config_save_internal()`**

In `_config_save_internal()`, immediately before `cJSON_AddItemToObject(root, "sensors", sensors);`, add:

```c
    cJSON *alarms = cJSON_CreateObject();
    cJSON_AddBoolToObject(alarms, "temp_enabled", s_config.alarm_temp_enabled);
    cJSON_AddNumberToObject(alarms, "temp_low",   s_config.alarm_temp_low);
    cJSON_AddNumberToObject(alarms, "temp_high",  s_config.alarm_temp_high);
    cJSON_AddBoolToObject(alarms, "hum_enabled",  s_config.alarm_hum_enabled);
    cJSON_AddNumberToObject(alarms, "hum_low",    s_config.alarm_hum_low);
    cJSON_AddNumberToObject(alarms, "hum_high",   s_config.alarm_hum_high);
    cJSON_AddBoolToObject(alarms, "tc_enabled",   s_config.alarm_tc_enabled);
    cJSON_AddNumberToObject(alarms, "tc_low",     s_config.alarm_tc_low);
    cJSON_AddNumberToObject(alarms, "tc_high",    s_config.alarm_tc_high);
    cJSON_AddItemToObject(sensors, "alarms", alarms);
```

- [ ] **Step 4: Add JSON load in `config_load()`**

In `config_load()`, at the end of the `if (sensors)` block (after all the existing `thermocouple_correction` load), add:

```c
        cJSON *alarms = cJSON_GetObjectItem(sensors, "alarms");
        if (alarms) {
            if ((item = cJSON_GetObjectItem(alarms, "temp_enabled")) && cJSON_IsBool(item))
                s_config.alarm_temp_enabled = cJSON_IsTrue(item);
            if ((item = cJSON_GetObjectItem(alarms, "temp_low")) && cJSON_IsNumber(item))
                s_config.alarm_temp_low = (float)item->valuedouble;
            if ((item = cJSON_GetObjectItem(alarms, "temp_high")) && cJSON_IsNumber(item))
                s_config.alarm_temp_high = (float)item->valuedouble;
            if ((item = cJSON_GetObjectItem(alarms, "hum_enabled")) && cJSON_IsBool(item))
                s_config.alarm_hum_enabled = cJSON_IsTrue(item);
            if ((item = cJSON_GetObjectItem(alarms, "hum_low")) && cJSON_IsNumber(item))
                s_config.alarm_hum_low = (float)item->valuedouble;
            if ((item = cJSON_GetObjectItem(alarms, "hum_high")) && cJSON_IsNumber(item))
                s_config.alarm_hum_high = (float)item->valuedouble;
            if ((item = cJSON_GetObjectItem(alarms, "tc_enabled")) && cJSON_IsBool(item))
                s_config.alarm_tc_enabled = cJSON_IsTrue(item);
            if ((item = cJSON_GetObjectItem(alarms, "tc_low")) && cJSON_IsNumber(item))
                s_config.alarm_tc_low = (float)item->valuedouble;
            if ((item = cJSON_GetObjectItem(alarms, "tc_high")) && cJSON_IsNumber(item))
                s_config.alarm_tc_high = (float)item->valuedouble;
        }
```

- [ ] **Step 5: Add getters/setters at the end of `config_manager.c`**

Append before the final blank line:

```c
// ============================================================================
// Getters/Setters - Alarm Thresholds
// ============================================================================

bool  config_get_alarm_temp_enabled(void) { return s_config.alarm_temp_enabled; }
float config_get_alarm_temp_low(void)     { return s_config.alarm_temp_low; }
float config_get_alarm_temp_high(void)    { return s_config.alarm_temp_high; }
bool  config_get_alarm_hum_enabled(void)  { return s_config.alarm_hum_enabled; }
float config_get_alarm_hum_low(void)      { return s_config.alarm_hum_low; }
float config_get_alarm_hum_high(void)     { return s_config.alarm_hum_high; }
bool  config_get_alarm_tc_enabled(void)   { return s_config.alarm_tc_enabled; }
float config_get_alarm_tc_low(void)       { return s_config.alarm_tc_low; }
float config_get_alarm_tc_high(void)      { return s_config.alarm_tc_high; }

void config_set_alarm_temp_enabled(bool e) { s_config.alarm_temp_enabled = e; }
void config_set_alarm_temp_low(float v)    { s_config.alarm_temp_low = v; }
void config_set_alarm_temp_high(float v)   { s_config.alarm_temp_high = v; }
void config_set_alarm_hum_enabled(bool e)  { s_config.alarm_hum_enabled = e; }
void config_set_alarm_hum_low(float v)     { s_config.alarm_hum_low = v; }
void config_set_alarm_hum_high(float v)    { s_config.alarm_hum_high = v; }
void config_set_alarm_tc_enabled(bool e)   { s_config.alarm_tc_enabled = e; }
void config_set_alarm_tc_low(float v)      { s_config.alarm_tc_low = v; }
void config_set_alarm_tc_high(float v)     { s_config.alarm_tc_high = v; }
```

- [ ] **Step 6: Declare getters/setters in `config_manager.h`**

In `main/config/config_manager.h`, after the `void config_set_thermocouple_correction(float correction);` line, add:

```c
// ============================================================================
// Alarm Threshold Configuration
// ============================================================================

bool  config_get_alarm_temp_enabled(void);
float config_get_alarm_temp_low(void);
float config_get_alarm_temp_high(void);
bool  config_get_alarm_hum_enabled(void);
float config_get_alarm_hum_low(void);
float config_get_alarm_hum_high(void);
bool  config_get_alarm_tc_enabled(void);
float config_get_alarm_tc_low(void);
float config_get_alarm_tc_high(void);

void config_set_alarm_temp_enabled(bool enabled);
void config_set_alarm_temp_low(float val);
void config_set_alarm_temp_high(float val);
void config_set_alarm_hum_enabled(bool enabled);
void config_set_alarm_hum_low(float val);
void config_set_alarm_hum_high(float val);
void config_set_alarm_tc_enabled(bool enabled);
void config_set_alarm_tc_low(float val);
void config_set_alarm_tc_high(float val);
```

- [ ] **Step 7: Build to verify no errors**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build
```
Expected: `Project build complete.` with no errors.

- [ ] **Step 8: Commit**

```bash
git add main/config/config_manager.h main/config/config_manager.c
git commit -m "feat(config): add alarm threshold fields (9 channels, disabled by default)"
```

---

## Task 2: alarm_manager Module

**Files:**
- Create: `main/alarm/alarm_manager.h`
- Create: `main/alarm/alarm_manager.c`

- [ ] **Step 1: Create `main/alarm/alarm_manager.h`**

```c
/**
 * @file alarm_manager.h
 * @brief Per-channel threshold alarm manager
 *
 * Evaluates sensor readings against configured thresholds.
 * State machine per channel: IDLE → ACTIVE → ACKNOWLEDGED → IDLE.
 * Drives buzzer siren when any channel is ACTIVE.
 */

#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"
#include "sensor_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Info about the highest-priority active alarm (for OLED display)
 */
typedef struct {
    char  label[24];    // e.g. "TEMP ALTA", "UMID BAIXA"
    float value;        // current sensor reading
    float threshold;    // the violated limit
    bool  is_high;      // true = above high limit, false = below low limit
    int   extra_count;  // number of additional active alarms beyond this one
} alarm_info_t;

/**
 * @brief Initialize alarm manager (creates mutex, resets channels)
 */
esp_err_t alarm_manager_init(void);

/**
 * @brief Evaluate all channels against current sensor data.
 *        Call this after every sensor read (from sensor_task).
 */
void alarm_manager_evaluate(const sensor_data_t *data);

/**
 * @brief Acknowledge all active alarms (silence siren, keep display normal).
 *        Called by button_handler on short press when alarm is active.
 */
void alarm_manager_acknowledge(void);

/**
 * @brief Returns true if any channel is in ACTIVE state (not ACKNOWLEDGED).
 */
bool alarm_manager_is_active(void);

/**
 * @brief Fill *info with the highest-priority active alarm details.
 * @return true if an active alarm exists, false if none.
 */
bool alarm_manager_get_active_info(alarm_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* ALARM_MANAGER_H */
```

- [ ] **Step 2: Create `main/alarm/alarm_manager.c`**

```c
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

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // Channel 0: I2C temperature
    if (data->temp_hum_valid && config_get_alarm_temp_enabled()) {
        evaluate_channel(&s_channels[0], data->temperature,
                         config_get_alarm_temp_low(), config_get_alarm_temp_high(),
                         "TEMP ALTA", "TEMP BAIXA");
    } else {
        s_channels[0].state = ALARM_STATE_IDLE;
    }

    // Channel 1: I2C humidity
    if (data->temp_hum_valid && config_get_alarm_hum_enabled()) {
        evaluate_channel(&s_channels[1], data->humidity,
                         config_get_alarm_hum_low(), config_get_alarm_hum_high(),
                         "UMID ALTA", "UMID BAIXA");
    } else {
        s_channels[1].state = ALARM_STATE_IDLE;
    }

    // Channel 2: thermocouple
    if (data->thermocouple_valid && config_get_alarm_tc_enabled()) {
        evaluate_channel(&s_channels[2], data->thermocouple_temp,
                         config_get_alarm_tc_low(), config_get_alarm_tc_high(),
                         "TERM ALTA", "TERM BAIXA");
    } else {
        s_channels[2].state = ALARM_STATE_IDLE;
    }

    // Start or stop siren task based on active channels
    bool any_active = (s_channels[0].state == ALARM_STATE_ACTIVE ||
                       s_channels[1].state == ALARM_STATE_ACTIVE ||
                       s_channels[2].state == ALARM_STATE_ACTIVE);

    if (any_active && s_siren_task == NULL) {
        xTaskCreatePinnedToCore(alarm_siren_task, "siren", 2048, NULL, 3, &s_siren_task, 0);
        ESP_LOGI(TAG, "Siren started");
    } else if (!any_active && s_siren_task != NULL) {
        vTaskDelete(s_siren_task);
        s_siren_task = NULL;
        buzzer_off();
        ESP_LOGI(TAG, "Siren stopped (all channels cleared)");
    }

    xSemaphoreGive(s_mutex);
}

void alarm_manager_acknowledge(void)
{
    if (!s_initialized) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (int i = 0; i < 3; i++) {
        if (s_channels[i].state == ALARM_STATE_ACTIVE) {
            s_channels[i].state = ALARM_STATE_ACKNOWLEDGED;
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
```

- [ ] **Step 3: Build is not possible yet (depends on CMakeLists). Skip to Task 6 build.**

- [ ] **Step 4: Commit**

```bash
git add main/alarm/alarm_manager.h main/alarm/alarm_manager.c
git commit -m "feat(alarm): add alarm_manager module with 3-channel state machine"
```

---

## Task 3: button_handler Module

**Files:**
- Create: `main/interface/button_handler.h`
- Create: `main/interface/button_handler.c`

- [ ] **Step 1: Create `main/interface/button_handler.h`**

```c
/**
 * @file button_handler.h
 * @brief Physical button handler for GPIO25
 *
 * Short press (<2s):
 *   - If alarm active: alarm_manager_acknowledge()
 *   - Else: oled_display_next_page()
 *
 * Long press (≥30s): factory reset (config_reset_defaults + restart)
 */

#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_GPIO     25

/**
 * @brief Initialize button handler: configure GPIO25, install ISR, create task.
 * @return ESP_OK on success
 */
esp_err_t button_handler_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_HANDLER_H */
```

- [ ] **Step 2: Create `main/interface/button_handler.c`**

```c
/**
 * @file button_handler.c
 * @brief GPIO25 button: short press (navigate/acknowledge) and long press (factory reset)
 *
 * Uses falling-edge ISR + task notification for responsive input.
 * Debounce: 50ms. Short press: < 2000ms. Long press: ≥ 30000ms.
 */

#include "button_handler.h"
#include "alarm_manager.h"
#include "oled_display.h"
#include "config_manager.h"
#include "buzzer.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUTTON";

#define DEBOUNCE_US         50000   // 50ms in microseconds
#define SHORT_PRESS_MAX_MS  2000
#define LONG_PRESS_MS       30000

static TaskHandle_t    s_task_handle = NULL;
static volatile int64_t s_last_isr_us = 0;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    int64_t now = esp_timer_get_time();
    if ((now - s_last_isr_us) < DEBOUNCE_US) return;
    s_last_isr_us = now;

    BaseType_t higher = pdFALSE;
    vTaskNotifyGiveFromISR(s_task_handle, &higher);
    portYIELD_FROM_ISR(higher);
}

static void button_task(void *param)
{
    while (1) {
        // Block until ISR fires (button pressed)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        int64_t press_start_us = esp_timer_get_time();
        bool long_press_fired  = false;

        // Poll until button is released or long press threshold reached
        while (gpio_get_level(BUTTON_GPIO) == 0) {
            int64_t held_ms = (esp_timer_get_time() - press_start_us) / 1000;

            if (!long_press_fired && held_ms >= LONG_PRESS_MS) {
                long_press_fired = true;
                ESP_LOGW(TAG, "Long press: factory reset");
                buzzer_beep_pattern(3, 200, 100);
                config_reset_defaults();
                config_save();
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }

        if (!long_press_fired) {
            int64_t held_ms = (esp_timer_get_time() - press_start_us) / 1000;
            if (held_ms < SHORT_PRESS_MAX_MS) {
                if (alarm_manager_is_active()) {
                    ESP_LOGI(TAG, "Short press: acknowledge alarm");
                    alarm_manager_acknowledge();
                } else {
                    ESP_LOGI(TAG, "Short press: next page");
                    oled_display_next_page();
                }
            }
        }
    }
}

esp_err_t button_handler_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask  = (1ULL << BUTTON_GPIO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_NEGEDGE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    xTaskCreatePinnedToCore(button_task, "button", 2048, NULL, 4, &s_task_handle, 0);

    // Install ISR service (ignore ESP_ERR_INVALID_STATE = already installed)
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Button handler initialized (GPIO%d)", BUTTON_GPIO);
    return ESP_OK;
}
```

- [ ] **Step 3: Commit**

```bash
git add main/interface/button_handler.h main/interface/button_handler.c
git commit -m "feat(button): add button_handler for GPIO25 (short press + 30s factory reset)"
```

---

## Task 4: oled_display — Alarm Page + Invert Reset

**Files:**
- Modify: `main/display/oled_display.h`
- Modify: `main/display/oled_display.c`

- [ ] **Step 1: Add `oled_display_show_alarm` declaration to `oled_display.h`**

In `main/display/oled_display.h`, add the `alarm_manager.h` include and the new function declaration. After the `#include "esp_err.h"` line, add:

```c
#include "alarm_manager.h"
```

After the `oled_display_get_page()` declaration (before `#ifdef __cplusplus` closing block), add:

```c
/**
 * @brief Show alarm page with 1Hz blinking header.
 *        Toggles display inversion each call (call at 500ms interval for 1Hz blink).
 *        Resets inversion to normal automatically when alarm clears.
 *
 * @param info Active alarm details from alarm_manager_get_active_info()
 */
void oled_display_show_alarm(const alarm_info_t *info);
```

- [ ] **Step 2: Add `oled_display_show_alarm` implementation to `oled_display.c`**

Append at the end of `main/display/oled_display.c` (after `oled_display_get_page()`):

```c
void oled_display_show_alarm(const alarm_info_t *info)
{
    if (!oled_initialized || !info) return;

    // Toggle hardware invert each call → 1Hz blink at 500ms call rate
    static uint8_t blink_ctr = 0;
    oled_send_cmd((blink_ctr % 2 == 0) ? 0xA7 : SSD1306_CMD_SET_NORMAL);
    blink_ctr++;

    oled_display_clear();

    // Page 0: header
    oled_display_text(0, 0, "*** ALARME ***");

    // Page 2: label (+ extra count if multiple alarms)
    if (info->extra_count > 0) {
        char label_line[24];
        snprintf(label_line, sizeof(label_line), "%s +%d", info->label, info->extra_count);
        oled_display_text(0, 2, label_line);
    } else {
        oled_display_text(0, 2, info->label);
    }

    // Page 3: current value
    char val_str[22];
    snprintf(val_str, sizeof(val_str), "Atual: %.1f", info->value);
    oled_display_text(0, 3, val_str);

    // Page 4: violated threshold
    char thr_str[22];
    snprintf(thr_str, sizeof(thr_str), "Limite: %.1f", info->threshold);
    oled_display_text(0, 4, thr_str);

    // Page 6: hint
    oled_display_text(0, 6, "Btn p/silenciar");

    oled_display_update();
}
```

- [ ] **Step 3: Reset display inversion in `oled_display_show_sensors()` and `oled_display_show_system()`**

In `oled_display_show_sensors()`, after the `if (!oled_initialized) return;` line, add:

```c
    oled_send_cmd(SSD1306_CMD_SET_NORMAL);  // ensure not inverted from alarm page
```

In `oled_display_show_system()`, after the `if (!oled_initialized) return;` line, add:

```c
    oled_send_cmd(SSD1306_CMD_SET_NORMAL);  // ensure not inverted from alarm page
```

- [ ] **Step 4: Commit**

```bash
git add main/display/oled_display.h main/display/oled_display.c
git commit -m "feat(oled): add alarm page with 1Hz blink; reset invert in normal pages"
```

---

## Task 5: sensor_manager — Call alarm_manager_evaluate

**Files:**
- Modify: `main/sensors/sensor_manager.c`

- [ ] **Step 1: Add include**

In `main/sensors/sensor_manager.c`, after `#include "config_manager.h"`, add:

```c
#include "alarm_manager.h"
```

- [ ] **Step 2: Call `alarm_manager_evaluate` in `sensor_task`**

In `sensor_task()`, the loop body currently reads:

```c
        vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));
        sensor_manager_read();
```

Change it to:

```c
        vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));
        sensor_manager_read();

        sensor_data_t alarm_data;
        sensor_manager_get_data(&alarm_data);
        alarm_manager_evaluate(&alarm_data);
```

- [ ] **Step 3: Commit**

```bash
git add main/sensors/sensor_manager.c
git commit -m "feat(sensor): evaluate alarm thresholds after each sensor read"
```

---

## Task 6: CMakeLists.txt + main.c Integration

**Files:**
- Modify: `main/CMakeLists.txt`
- Modify: `main/main.c`

- [ ] **Step 1: Update `CMakeLists.txt`**

In `main/CMakeLists.txt`, add the two new source files. In the `SRCS` block, after `"clock_sync/clock_sync.c"`, add:

```cmake
        "alarm/alarm_manager.c"
        "interface/button_handler.c"
```

In the `INCLUDE_DIRS` block, after `"clock_sync"`, add:

```cmake
        "alarm"
```

(`interface` is already in INCLUDE_DIRS.)

- [ ] **Step 2: Add includes to `main.c`**

In `main/main.c`, after `#include "clock_sync.h"`, add:

```c
#include "alarm_manager.h"
#include "button_handler.h"
```

- [ ] **Step 3: Update `display_task` in `main.c`**

Replace the entire `display_task` function body (the `while (1)` loop and the `page_timer`/`page_cycle_ms` variables) with:

```c
static void display_task(void *param)
{
    ESP_LOGI(TAG, "Display task started");

    vTaskDelay(pdMS_TO_TICKS(5000));

    void *i2c_bus = sensor_manager_get_i2c_bus();
    if (!i2c_bus) {
        ESP_LOGW(TAG, "I2C bus not available, display task exiting");
        vTaskDelete(NULL);
        return;
    }

    if (oled_display_init(i2c_bus) != ESP_OK) {
        ESP_LOGW(TAG, "OLED not found, display task exiting");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        if (alarm_manager_is_active()) {
            alarm_info_t info;
            alarm_manager_get_active_info(&info);
            oled_display_show_alarm(&info);
        } else {
            oled_page_t page = oled_display_get_page();
            switch (page) {
                case OLED_PAGE_SENSORS: {
                    sensor_data_t data;
                    sensor_manager_get_data(&data);
                    float tc = data.thermocouple_valid ? data.thermocouple_temp : NAN;
                    float t  = data.temp_hum_valid ? data.temperature : NAN;
                    float h  = data.temp_hum_valid ? data.humidity : NAN;
                    oled_display_show_sensors(t, h, tc);
                    break;
                }
                case OLED_PAGE_SYSTEM: {
                    char ip[16] = "N/A";
                    wifi_manager_get_ip(ip);
                    uint32_t uptime_s  = xTaskGetTickCount() / configTICK_RATE_HZ;
                    uint32_t free_heap = esp_get_free_heap_size();
                    lorawan_stats_t lora_stats;
                    lorawan_get_stats(&lora_stats);
                    oled_display_show_system(ip, uptime_s, free_heap,
                                             lora_stats.joined, lora_stats.dev_addr,
                                             lora_stats.uplink_count, lora_stats.last_rssi,
                                             lora_stats.last_snr);
                    break;
                }
                default:
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

- [ ] **Step 4: Add `alarm_manager_init()` and `button_handler_init()` to `app_main()`**

In `app_main()`, after the `clock_sync_init();` line, add:

```c
    // Initialize alarm manager
    if (alarm_manager_init() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize alarm manager");
    }

    // Initialize button handler (GPIO25)
    if (button_handler_init() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize button handler");
    }
```

- [ ] **Step 5: Build to verify compilation**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build
```
Expected: `Project build complete.` with no errors or warnings about undefined symbols.

- [ ] **Step 6: Commit**

```bash
git add main/CMakeLists.txt main/main.c
git commit -m "feat(main): integrate alarm_manager and button_handler; remove display auto-cycle"
```

---

## Task 7: Web API — Alarm Thresholds

**Files:**
- Modify: `main/webserver/handlers/api_sensors.c`

- [ ] **Step 1: Add alarm fields to GET `/api/sensors/config`**

In `api_sensors_config_get_handler()`, after the `cJSON_AddNumberToObject(root, "buzzer_volume", config_get_buzzer_volume());` line, add:

```c
    cJSON_AddBoolToObject(root, "alarm_temp_enabled", config_get_alarm_temp_enabled());
    cJSON_AddNumberToObject(root, "alarm_temp_low",   config_get_alarm_temp_low());
    cJSON_AddNumberToObject(root, "alarm_temp_high",  config_get_alarm_temp_high());
    cJSON_AddBoolToObject(root, "alarm_hum_enabled",  config_get_alarm_hum_enabled());
    cJSON_AddNumberToObject(root, "alarm_hum_low",    config_get_alarm_hum_low());
    cJSON_AddNumberToObject(root, "alarm_hum_high",   config_get_alarm_hum_high());
    cJSON_AddBoolToObject(root, "alarm_tc_enabled",   config_get_alarm_tc_enabled());
    cJSON_AddNumberToObject(root, "alarm_tc_low",     config_get_alarm_tc_low());
    cJSON_AddNumberToObject(root, "alarm_tc_high",    config_get_alarm_tc_high());
```

- [ ] **Step 2: Add alarm fields to POST `/api/sensors/config`**

In `api_sensors_config_post_handler()`, after the `buzzer_volume` parsing block (the block ending with `buzzer_beep(100);`), add:

```c
    if ((item = cJSON_GetObjectItem(json, "alarm_temp_enabled")) && cJSON_IsBool(item))
        config_set_alarm_temp_enabled(cJSON_IsTrue(item));
    if ((item = cJSON_GetObjectItem(json, "alarm_temp_low")) && cJSON_IsNumber(item))
        config_set_alarm_temp_low((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_temp_high")) && cJSON_IsNumber(item))
        config_set_alarm_temp_high((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_hum_enabled")) && cJSON_IsBool(item))
        config_set_alarm_hum_enabled(cJSON_IsTrue(item));
    if ((item = cJSON_GetObjectItem(json, "alarm_hum_low")) && cJSON_IsNumber(item))
        config_set_alarm_hum_low((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_hum_high")) && cJSON_IsNumber(item))
        config_set_alarm_hum_high((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_enabled")) && cJSON_IsBool(item))
        config_set_alarm_tc_enabled(cJSON_IsTrue(item));
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_low")) && cJSON_IsNumber(item))
        config_set_alarm_tc_low((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_high")) && cJSON_IsNumber(item))
        config_set_alarm_tc_high((float)item->valuedouble);
```

- [ ] **Step 3: Build**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build
```
Expected: `Project build complete.`

- [ ] **Step 4: Commit**

```bash
git add main/webserver/handlers/api_sensors.c
git commit -m "feat(api): expose alarm thresholds in GET/POST /api/sensors/config"
```

---

## Task 8: Web UI — Alarmes Section

**Files:**
- Modify: `main/www/tabs/config.html`
- Modify: `main/www/tabs/config.js`

- [ ] **Step 1: Add Alarmes card to `config.html`**

In `main/www/tabs/config.html`, after the closing `</div>` of the Buzzer card (after `<button onclick="saveBuzzerConfig()" ...`), add:

```html
<div class="card"><h3>Alarmes</h3>
<div class="form-row"><label>Temp Ext. Ativo</label><input type="checkbox" id="cfg-alarm-temp-en"></div>
<div class="form-row"><label>Temp Ext. Min (°C)</label><input type="number" id="cfg-alarm-temp-low" step="0.1" value="0"></div>
<div class="form-row"><label>Temp Ext. Max (°C)</label><input type="number" id="cfg-alarm-temp-high" step="0.1" value="0"></div>
<div class="form-row"><label>Umidade Ativo</label><input type="checkbox" id="cfg-alarm-hum-en"></div>
<div class="form-row"><label>Umidade Min (%)</label><input type="number" id="cfg-alarm-hum-low" step="0.1" min="0" max="100" value="0"></div>
<div class="form-row"><label>Umidade Max (%)</label><input type="number" id="cfg-alarm-hum-high" step="0.1" min="0" max="100" value="0"></div>
<div class="form-row"><label>Termopar Ativo</label><input type="checkbox" id="cfg-alarm-tc-en"></div>
<div class="form-row"><label>Termopar Min (°C)</label><input type="number" id="cfg-alarm-tc-low" step="0.1" value="0"></div>
<div class="form-row"><label>Termopar Max (°C)</label><input type="number" id="cfg-alarm-tc-high" step="0.1" value="0"></div>
<button onclick="saveAlarmConfig()" class="btn full mt-2">Salvar Alarmes</button></div>
```

- [ ] **Step 2: Add alarm functions to `config.js`**

In `main/www/tabs/config.js`, in `loadSensorConfig()`, after the buzzer volume lines, add:

```js
        document.getElementById('cfg-alarm-temp-en').checked    = d.alarm_temp_enabled || false;
        document.getElementById('cfg-alarm-temp-low').value     = d.alarm_temp_low  !== undefined ? d.alarm_temp_low  : 0;
        document.getElementById('cfg-alarm-temp-high').value    = d.alarm_temp_high !== undefined ? d.alarm_temp_high : 0;
        document.getElementById('cfg-alarm-hum-en').checked     = d.alarm_hum_enabled || false;
        document.getElementById('cfg-alarm-hum-low').value      = d.alarm_hum_low   !== undefined ? d.alarm_hum_low   : 0;
        document.getElementById('cfg-alarm-hum-high').value     = d.alarm_hum_high  !== undefined ? d.alarm_hum_high  : 0;
        document.getElementById('cfg-alarm-tc-en').checked      = d.alarm_tc_enabled || false;
        document.getElementById('cfg-alarm-tc-low').value       = d.alarm_tc_low    !== undefined ? d.alarm_tc_low    : 0;
        document.getElementById('cfg-alarm-tc-high').value      = d.alarm_tc_high   !== undefined ? d.alarm_tc_high   : 0;
```

After the `saveBuzzerConfig` function, add:

```js
// ============================================================================
// Alarm Configuration
// ============================================================================

async function saveAlarmConfig() {
    const config = {
        alarm_temp_enabled: document.getElementById('cfg-alarm-temp-en').checked,
        alarm_temp_low:     parseFloat(document.getElementById('cfg-alarm-temp-low').value),
        alarm_temp_high:    parseFloat(document.getElementById('cfg-alarm-temp-high').value),
        alarm_hum_enabled:  document.getElementById('cfg-alarm-hum-en').checked,
        alarm_hum_low:      parseFloat(document.getElementById('cfg-alarm-hum-low').value),
        alarm_hum_high:     parseFloat(document.getElementById('cfg-alarm-hum-high').value),
        alarm_tc_enabled:   document.getElementById('cfg-alarm-tc-en').checked,
        alarm_tc_low:       parseFloat(document.getElementById('cfg-alarm-tc-low').value),
        alarm_tc_high:      parseFloat(document.getElementById('cfg-alarm-tc-high').value),
    };

    try {
        const r = await api('sensors/config', 'POST', config);
        if (r.success) {
            toast('Configuração de alarmes salva', 'success');
        } else {
            toast(r.message || 'Falha ao salvar', 'error');
        }
    } catch (e) {
        toast('Falha ao salvar alarmes', 'error');
    }
}
```

- [ ] **Step 3: Build firmware (www changes don't need idf.py build, but verify no firmware regressions)**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build
```
Expected: `Project build complete.`

- [ ] **Step 4: Commit**

```bash
git add main/www/tabs/config.html main/www/tabs/config.js
git commit -m "feat(ui): add Alarmes section to config tab"
```

---

## Manual Verification Checklist

After flashing (`./flash.sh update`):

- [ ] Boot: OLED shows sensor page, does NOT auto-cycle to system page
- [ ] Short press: OLED switches between sensor page and system page
- [ ] Enable a threshold via web UI (e.g., temp high = 1.0°C to force trigger)
- [ ] Save → verify buzzer plays 3-beep pattern with 2s pause, repeating
- [ ] Verify OLED shows alarm page with correct label, value, and threshold; header blinks
- [ ] Short press while alarm active → buzzer stops, OLED returns to sensor page
- [ ] After acknowledge, set threshold back to a wide range → value re-enters normal → alarm can trigger again on next violation
- [ ] Hold button 30s → 3 long beeps → device restarts with factory defaults
