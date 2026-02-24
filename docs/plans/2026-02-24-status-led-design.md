# Status LED WS2812 - Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a WS2812 addressable RGB LED on GPIO2 for visual system status indication (error/WiFi/LoRaWAN/normal states).

**Architecture:** A `status_led` driver using ESP-IDF's `led_strip` component (RMT-based). The health monitor task evaluates system state every 10s and calls the LED driver to blink the appropriate color. All parameters (colors, brightness, interval, on/off) are configurable via the existing JSON config system.

**Tech Stack:** ESP-IDF v5.5.3, `led_strip` component, RMT peripheral, FreeRTOS

---

### Task 1: Add `led_strip` dependency

**Files:**
- Modify: `main/idf_component.yml`

**Step 1: Add the espressif/led_strip component dependency**

```yaml
dependencies:
  joltwallet/littlefs: "*"
  jgromes/radiolib: "^7.5.0"
  espressif/led_strip: "^3.0.0"
```

**Step 2: Verify dependency resolves**

Run: `. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && cd /home/felipe/work/isi-latecme-quality-enddevice-2 && idf.py reconfigure`
Expected: Component manager downloads `led_strip`, no errors.

**Step 3: Commit**

```bash
git add main/idf_component.yml
git commit -m "feat(led): add led_strip component dependency"
```

---

### Task 2: Add LED config fields to config_manager

**Files:**
- Modify: `main/config/config_manager.c:18-53` (struct), `:98-138` (defaults), `:144-213` (save), `:215-370` (load), `:498-503` (interface getters)
- Modify: `main/config/config_manager.h:102-108` (interface section)

**Step 1: Add LED fields to config_t struct (config_manager.c:46-47)**

After `uint8_t buzzer_volume;` add:

```c
    // LED
    bool led_enabled;
    uint8_t led_brightness;        // 0-100%
    uint32_t led_blink_interval_ms;
    char led_color_normal[8];      // "#RRGGBB" + null
    char led_color_lorawan[8];
    char led_color_wifi[8];
    char led_color_error[8];
```

**Step 2: Add LED defaults in config_reset_defaults() (config_manager.c:129-130)**

After `s_config.buzzer_volume = 30;` add:

```c
    s_config.led_enabled = true;
    s_config.led_brightness = 50;
    s_config.led_blink_interval_ms = 30000;
    strcpy(s_config.led_color_normal, "#FFFFFF");
    strcpy(s_config.led_color_lorawan, "#FFB000");
    strcpy(s_config.led_color_wifi, "#0000FF");
    strcpy(s_config.led_color_error, "#FF0000");
```

**Step 3: Add LED save in _config_save_internal() (config_manager.c:183)**

After `cJSON_AddNumberToObject(interface, "buzzer_volume", s_config.buzzer_volume);` add:

```c
    cJSON *led = cJSON_CreateObject();
    cJSON_AddBoolToObject(led, "enabled", s_config.led_enabled);
    cJSON_AddNumberToObject(led, "brightness", s_config.led_brightness);
    cJSON_AddNumberToObject(led, "blink_interval_ms", s_config.led_blink_interval_ms);
    cJSON_AddStringToObject(led, "color_normal", s_config.led_color_normal);
    cJSON_AddStringToObject(led, "color_lorawan", s_config.led_color_lorawan);
    cJSON_AddStringToObject(led, "color_wifi", s_config.led_color_wifi);
    cJSON_AddStringToObject(led, "color_error", s_config.led_color_error);
    cJSON_AddItemToObject(interface, "led", led);
```

**Step 4: Add LED load in config_load() (config_manager.c:341-348)**

Inside the `if (interface)` block, after buzzer_volume parsing, add:

```c
        cJSON *led = cJSON_GetObjectItem(interface, "led");
        if (led) {
            if ((item = cJSON_GetObjectItem(led, "enabled")) && cJSON_IsBool(item)) {
                s_config.led_enabled = cJSON_IsTrue(item);
            }
            if ((item = cJSON_GetObjectItem(led, "brightness")) && cJSON_IsNumber(item)) {
                uint8_t b = (uint8_t)item->valueint;
                s_config.led_brightness = (b > 100) ? 100 : b;
            }
            if ((item = cJSON_GetObjectItem(led, "blink_interval_ms")) && cJSON_IsNumber(item)) {
                s_config.led_blink_interval_ms = (uint32_t)item->valueint;
            }
            if ((item = cJSON_GetObjectItem(led, "color_normal")) && cJSON_IsString(item)) {
                strncpy(s_config.led_color_normal, item->valuestring, sizeof(s_config.led_color_normal) - 1);
            }
            if ((item = cJSON_GetObjectItem(led, "color_lorawan")) && cJSON_IsString(item)) {
                strncpy(s_config.led_color_lorawan, item->valuestring, sizeof(s_config.led_color_lorawan) - 1);
            }
            if ((item = cJSON_GetObjectItem(led, "color_wifi")) && cJSON_IsString(item)) {
                strncpy(s_config.led_color_wifi, item->valuestring, sizeof(s_config.led_color_wifi) - 1);
            }
            if ((item = cJSON_GetObjectItem(led, "color_error")) && cJSON_IsString(item)) {
                strncpy(s_config.led_color_error, item->valuestring, sizeof(s_config.led_color_error) - 1);
            }
        }
```

**Step 5: Add getters/setters (config_manager.c:502-503, after buzzer)**

```c
bool config_get_led_enabled(void) { return s_config.led_enabled; }
uint8_t config_get_led_brightness(void) { return s_config.led_brightness; }
uint32_t config_get_led_blink_interval_ms(void) { return s_config.led_blink_interval_ms; }
const char* config_get_led_color_normal(void) { return s_config.led_color_normal; }
const char* config_get_led_color_lorawan(void) { return s_config.led_color_lorawan; }
const char* config_get_led_color_wifi(void) { return s_config.led_color_wifi; }
const char* config_get_led_color_error(void) { return s_config.led_color_error; }

void config_set_led_enabled(bool enabled) { s_config.led_enabled = enabled; }
void config_set_led_brightness(uint8_t brightness) { s_config.led_brightness = (brightness > 100) ? 100 : brightness; }
void config_set_led_blink_interval_ms(uint32_t ms) { s_config.led_blink_interval_ms = ms; }
void config_set_led_color_normal(const char *color) {
    if (color) strncpy(s_config.led_color_normal, color, sizeof(s_config.led_color_normal) - 1);
}
void config_set_led_color_lorawan(const char *color) {
    if (color) strncpy(s_config.led_color_lorawan, color, sizeof(s_config.led_color_lorawan) - 1);
}
void config_set_led_color_wifi(const char *color) {
    if (color) strncpy(s_config.led_color_wifi, color, sizeof(s_config.led_color_wifi) - 1);
}
void config_set_led_color_error(const char *color) {
    if (color) strncpy(s_config.led_color_error, color, sizeof(s_config.led_color_error) - 1);
}
```

**Step 6: Add declarations to header (config_manager.h:106-107)**

After `void config_set_buzzer_volume(uint8_t volume);` add:

```c
bool config_get_led_enabled(void);
uint8_t config_get_led_brightness(void);
uint32_t config_get_led_blink_interval_ms(void);
const char* config_get_led_color_normal(void);
const char* config_get_led_color_lorawan(void);
const char* config_get_led_color_wifi(void);
const char* config_get_led_color_error(void);

void config_set_led_enabled(bool enabled);
void config_set_led_brightness(uint8_t brightness);
void config_set_led_blink_interval_ms(uint32_t ms);
void config_set_led_color_normal(const char *color);
void config_set_led_color_lorawan(const char *color);
void config_set_led_color_wifi(const char *color);
void config_set_led_color_error(const char *color);
```

**Step 7: Build to verify**

Run: `idf.py build`
Expected: Compiles without errors.

**Step 8: Commit**

```bash
git add main/config/config_manager.c main/config/config_manager.h
git commit -m "feat(config): add LED status configuration fields"
```

---

### Task 3: Create status_led driver

**Files:**
- Create: `main/interface/status_led.h`
- Create: `main/interface/status_led.c`
- Modify: `main/CMakeLists.txt:11` (add to SRCS)

**Step 1: Create status_led.h**

```c
#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "esp_err.h"
#include "health_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STATUS_LED_GPIO 2

typedef enum {
    LED_STATE_NORMAL = 0,
    LED_STATE_LORAWAN_NOT_CONFIGURED,
    LED_STATE_WIFI_DISCONNECTED,
    LED_STATE_ERROR,
} led_state_t;

/**
 * @brief Initialize WS2812 LED on GPIO2
 * @return ESP_OK on success
 */
esp_err_t status_led_init(void);

/**
 * @brief Evaluate system health and blink LED accordingly.
 *        Call this periodically from health monitor task.
 * @param health Current system health snapshot
 */
void status_led_update(const system_health_t *health);

/**
 * @brief Deinitialize LED driver
 */
void status_led_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // STATUS_LED_H
```

**Step 2: Create status_led.c**

```c
#include "status_led.h"
#include <string.h>
#include <stdlib.h>
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config_manager.h"

static const char *TAG = "STATUS_LED";

static led_strip_handle_t s_led_strip = NULL;
static bool s_initialized = false;
static int64_t s_last_blink_time = 0;

// Heap threshold matching health_monitor.c
#define HEAP_WARNING_THRESHOLD 20000

/**
 * @brief Parse "#RRGGBB" hex string into r, g, b components
 */
static void parse_hex_color(const char *hex, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!hex || hex[0] != '#' || strlen(hex) < 7) {
        *r = *g = *b = 255; // fallback white
        return;
    }
    unsigned long val = strtoul(hex + 1, NULL, 16);
    *r = (val >> 16) & 0xFF;
    *g = (val >> 8) & 0xFF;
    *b = val & 0xFF;
}

/**
 * @brief Apply brightness scaling to a color component
 */
static uint8_t apply_brightness(uint8_t component, uint8_t brightness_pct)
{
    return (uint8_t)(((uint32_t)component * brightness_pct) / 100);
}

/**
 * @brief Set LED to color with brightness applied, then refresh
 */
static void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t brightness = config_get_led_brightness();
    led_strip_set_pixel(s_led_strip, 0,
                        apply_brightness(r, brightness),
                        apply_brightness(g, brightness),
                        apply_brightness(b, brightness));
    led_strip_refresh(s_led_strip);
}

/**
 * @brief Turn LED off
 */
static void led_off(void)
{
    led_strip_clear(s_led_strip);
    led_strip_refresh(s_led_strip);
}

/**
 * @brief Single blink: on for on_ms, then off
 */
static void blink_once(uint8_t r, uint8_t g, uint8_t b, uint32_t on_ms)
{
    led_set_color(r, g, b);
    vTaskDelay(pdMS_TO_TICKS(on_ms));
    led_off();
}

/**
 * @brief Determine LED state from health data
 */
static led_state_t evaluate_state(const system_health_t *health)
{
    // Priority 1: System error
    if (!health->filesystem_ok ||
        health->total_error_count > 0 ||
        health->free_heap < HEAP_WARNING_THRESHOLD) {
        return LED_STATE_ERROR;
    }

    // Priority 2: WiFi disconnected
    if (!health->wifi_connected) {
        return LED_STATE_WIFI_DISCONNECTED;
    }

    // Priority 3: LoRaWAN not configured (keys empty)
    const char *dev_eui = config_get_dev_eui();
    const char *app_key = config_get_app_key();
    const char *join_eui = config_get_join_eui();
    if (strlen(dev_eui) == 0 || strlen(app_key) == 0 || strlen(join_eui) == 0) {
        return LED_STATE_LORAWAN_NOT_CONFIGURED;
    }

    // Priority 4: All OK
    return LED_STATE_NORMAL;
}

esp_err_t status_led_init(void)
{
    if (s_initialized) return ESP_OK;

    if (!config_get_led_enabled()) {
        ESP_LOGI(TAG, "Status LED disabled in config");
        return ESP_OK;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = STATUS_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    led_off();
    s_initialized = true;
    s_last_blink_time = 0;
    ESP_LOGI(TAG, "Status LED initialized on GPIO%d", STATUS_LED_GPIO);
    return ESP_OK;
}

void status_led_update(const system_health_t *health)
{
    if (!s_initialized || !s_led_strip) return;
    if (!config_get_led_enabled()) return;

    led_state_t state = evaluate_state(health);
    uint8_t r, g, b;

    switch (state) {
        case LED_STATE_ERROR: {
            parse_hex_color(config_get_led_color_error(), &r, &g, &b);
            // Double blink pattern
            blink_once(r, g, b, 100);
            vTaskDelay(pdMS_TO_TICKS(100));
            blink_once(r, g, b, 100);
            s_last_blink_time = esp_timer_get_time();
            break;
        }
        case LED_STATE_WIFI_DISCONNECTED: {
            parse_hex_color(config_get_led_color_wifi(), &r, &g, &b);
            blink_once(r, g, b, 200);
            s_last_blink_time = esp_timer_get_time();
            break;
        }
        case LED_STATE_LORAWAN_NOT_CONFIGURED: {
            parse_hex_color(config_get_led_color_lorawan(), &r, &g, &b);
            blink_once(r, g, b, 200);
            s_last_blink_time = esp_timer_get_time();
            break;
        }
        case LED_STATE_NORMAL: {
            int64_t now = esp_timer_get_time();
            uint32_t interval_us = config_get_led_blink_interval_ms() * 1000;
            if (s_last_blink_time == 0 || (now - s_last_blink_time) >= interval_us) {
                parse_hex_color(config_get_led_color_normal(), &r, &g, &b);
                blink_once(r, g, b, 100);
                s_last_blink_time = now;
            }
            break;
        }
    }
}

void status_led_deinit(void)
{
    if (s_led_strip) {
        led_off();
        led_strip_del(s_led_strip);
        s_led_strip = NULL;
    }
    s_initialized = false;
}
```

**Step 3: Add to CMakeLists.txt SRCS (after line 11 `"interface/buzzer.c"`)**

```
        "interface/status_led.c"
```

**Step 4: Build to verify**

Run: `idf.py build`
Expected: Compiles without errors.

**Step 5: Commit**

```bash
git add main/interface/status_led.c main/interface/status_led.h main/CMakeLists.txt
git commit -m "feat(led): add WS2812 status LED driver with state evaluation"
```

---

### Task 4: Integrate LED into health monitor and main init

**Files:**
- Modify: `main/health/health_monitor.c:6-7` (include), `:55-85` (call update in task loop)
- Modify: `main/main.c:32` (include), `:228-233` (init after config)

**Step 1: Add include to health_monitor.c (after line 16)**

```c
#include "status_led.h"
```

**Step 2: Call status_led_update in health monitor task (health_monitor.c:83-84)**

Inside the `if (check_counter >= 5)` block, after the periodic status log section (after line 84), add:

```c
            // Update status LED based on current health
            status_led_update(&s_health);
```

**Step 3: Add include to main.c (after line 32 `#include "buzzer.h"`)**

```c
#include "status_led.h"
```

**Step 4: Initialize LED in app_main (main.c:228-233)**

After `buzzer_set_volume(config_get_buzzer_volume());` and the startup melody (after line 233), add:

```c
    // Initialize status LED
    if (status_led_init() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize status LED");
    }
```

**Step 5: Build to verify**

Run: `idf.py build`
Expected: Compiles without errors, no warnings.

**Step 6: Commit**

```bash
git add main/health/health_monitor.c main/main.c
git commit -m "feat(led): integrate status LED into health monitor and startup sequence"
```

---

### Task 5: Final build verification

**Step 1: Full clean build**

Run: `idf.py fullclean && idf.py build`
Expected: Builds successfully with no errors or warnings related to status_led.

**Step 2: Check binary size is reasonable**

Run: `idf.py size`
Expected: RMT + led_strip adds ~2-4KB to flash. No RAM concerns.

**Step 3: Commit any remaining changes**

If needed, commit. Otherwise this task is just verification.
