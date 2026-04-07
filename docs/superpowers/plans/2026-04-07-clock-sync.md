# TS003 Clock Sync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement LoRa Alliance TS003 Application Layer Clock Synchronization so the device syncs its RTC via LoRaWAN on boot and daily, enabling real timestamps in logs, OLED display, and clock-aligned uplink scheduling.

**Architecture:** RadioLib's built-in `addAppPackage(RADIOLIB_LORAWAN_PACKAGE_TS003, cb)` handles AppTimeAns delivery — when a downlink arrives on FPort 202, RadioLib calls the registered callback synchronously from within `sendReceive()`. The new `clock_sync` module owns the TS003 logic: it builds the AppTimeReq uplink, parses the AppTimeAns in its callback, and applies the correction via `settimeofday()`.

**Tech Stack:** ESP-IDF v5.5.3, FreeRTOS, RadioLib (jgromes__radiolib managed component), C/C++ boundary via `extern "C"`.

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `main/clock_sync/clock_sync.h` | Create | Public API: init, request, is_synced, get_time, task |
| `main/clock_sync/clock_sync.c` | Create | TS003 protocol logic, AppTimeReq/Ans encoding, settimeofday |
| `main/lorawan/lorawan_handler.h` | Modify | Add `lorawan_add_app_package()` C API |
| `main/lorawan/lorawan_handler.cpp` | Modify | Wrap RadioLib's `node->addAppPackage()` with mutex |
| `main/main.c` | Modify | Init clock_sync, spawn task, align uplinks after sync |
| `main/display/oled_display.c` | Modify | Show current time on system page |
| `main/CMakeLists.txt` | Modify | Add clock_sync source and include dir |
| `sdkconfig.defaults` | Modify | Enable system-time log timestamps |

---

## Context You Need

**Build command** (run from project root):
```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build
```

**LoRaWAN mutex rule:** `lorawan_mutex` must be held for all RadioLib calls. The `addAppPackage` callback is called from inside `sendReceive()`, which runs under the mutex. The callback must NOT call `lorawan_send()` — that would deadlock. `settimeofday()` is safe to call from within the mutex.

**RadioLib TS003 FPort:** `RADIOLIB_LORAWAN_FPORT_TS003 = 202`. Defined in `managed_components/jgromes__radiolib/src/protocols/LoRaWAN/LoRaWAN.h`.

**`lorawan_add_app_package()` must be called after `lorawan_init()`** (which creates the `node` object). It is safe to call it from `clock_sync_task()` after `lorawan_is_joined()` returns true — by that point `lorawan_init()` has already run.

**GPS epoch offset:** GPS epoch starts 1980-01-06; Unix epoch starts 1970-01-01. Difference = 315964800 seconds. `unix_time = device_time_gps + correction + 315964800`.

**AppTimeReq (5 bytes, little-endian):**
```
[0–3] DeviceTime  uint32_t  seconds since GPS epoch (0 before first sync)
[4]   Param       0x01      bit0=AnsRequired
```

**AppTimeAns (5 bytes, little-endian):**
```
[0–3] TimeCorrection  int32_t  server_time_gps - DeviceTime_sent
[4]   Param           uint8_t  bits[3:0] = TokenAns (ignored)
```

**OLED system page** uses all 8 rows (pages 0–7). Row 0 currently shows `"--- SYSTEM ---"`. Replace it with the formatted time when synced; keep the header when not synced.

---

## Task 1: Expose `addAppPackage` via C API in lorawan_handler

**Files:**
- Modify: `main/lorawan/lorawan_handler.h`
- Modify: `main/lorawan/lorawan_handler.cpp`

- [ ] **Step 1: Add typedef and declaration to lorawan_handler.h**

Open `main/lorawan/lorawan_handler.h`. After the existing `lorawan_stats_t` struct (around line 34), add:

```c
/**
 * @brief Callback type for LoRaWAN Application Package downlinks (TSxxx)
 *
 * Called from within lorawan_send() when a downlink is received on the
 * package's FPort. Do NOT call lorawan_send() from this callback.
 *
 * @param data  Downlink payload bytes
 * @param len   Payload length
 */
typedef void (*lorawan_package_cb_t)(uint8_t *data, size_t len);

/**
 * @brief Register a callback for a LoRaWAN Application Package (TSxxx)
 *
 * Wraps RadioLib's node->addAppPackage(). Must be called after the device
 * has joined (i.e., after lorawan_is_joined() returns true).
 *
 * @param package_id  One of RADIOLIB_LORAWAN_PACKAGE_TSxxx (e.g., RADIOLIB_LORAWAN_PACKAGE_TS003 = 1)
 * @param callback    Function called when a downlink arrives on the package's FPort
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t lorawan_add_app_package(uint8_t package_id, lorawan_package_cb_t callback);
```

- [ ] **Step 2: Implement lorawan_add_app_package() in lorawan_handler.cpp**

Open `main/lorawan/lorawan_handler.cpp`. Add after the `lorawan_force_rejoin()` function (before `lorawan_task()`):

```cpp
extern "C" esp_err_t lorawan_add_app_package(uint8_t package_id, lorawan_package_cb_t callback)
{
    if (!initialized || !node) {
        ESP_LOGE(TAG, "lorawan_add_app_package: not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(lorawan_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "lorawan_add_app_package: mutex timeout");
        return ESP_ERR_TIMEOUT;
    }

    int16_t state = node->addAppPackage(package_id, (PackageCb_t)callback);
    xSemaphoreGive(lorawan_mutex);

    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "addAppPackage failed: %d", state);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "App package %d registered (FPort 202)", package_id);
    return ESP_OK;
}
```

- [ ] **Step 3: Build to verify no compile errors**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build
```

Expected: build succeeds, no errors related to lorawan_handler.

- [ ] **Step 4: Commit**

```bash
git add main/lorawan/lorawan_handler.h main/lorawan/lorawan_handler.cpp
git commit -m "feat(lorawan): expose addAppPackage via C API for TS003 support"
```

---

## Task 2: Create clock_sync module

**Files:**
- Create: `main/clock_sync/clock_sync.h`
- Create: `main/clock_sync/clock_sync.c`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Create main/clock_sync/clock_sync.h**

```c
/**
 * @file clock_sync.h
 * @brief LoRaWAN Application Layer Clock Synchronization (TS003)
 *
 * Sends AppTimeReq on FPort 202 and applies the server's TimeCorrection
 * to the ESP32 system clock via settimeofday(). After sync, time(NULL)
 * returns valid UTC timestamps.
 */

#ifndef CLOCK_SYNC_H
#define CLOCK_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize clock sync module
 *
 * Registers the AppTimeAns callback with the LoRaWAN handler.
 * Must be called before spawning clock_sync_task, but may be called
 * before lorawan_init() — the callback is registered lazily in the task.
 *
 * @return ESP_OK always
 */
esp_err_t clock_sync_init(void);

/**
 * @brief Send an AppTimeReq uplink and wait for AppTimeAns
 *
 * Blocks until lorawan_send() returns. If the server replies, the
 * AppTimeAns callback applies settimeofday() before this returns.
 * Must be called after lorawan_is_joined() == true.
 *
 * @return ESP_OK if uplink sent (regardless of whether sync succeeded),
 *         ESP_FAIL on lorawan_send() error
 */
esp_err_t clock_sync_request(void);

/**
 * @brief Check if the clock has been synchronized at least once
 */
bool clock_sync_is_synced(void);

/**
 * @brief Get current Unix timestamp
 *
 * @return Current time_t, or 0 if not yet synced
 */
time_t clock_sync_get_time(void);

/**
 * @brief FreeRTOS task: syncs on boot, re-syncs every 24 hours
 *
 * Waits for LoRaWAN join, calls lorawan_add_app_package() to register
 * the TS003 callback, then calls clock_sync_request(). Repeats every 24h.
 *
 * Create with: xTaskCreatePinnedToCore(clock_sync_task, "clock_sync",
 *                                      3072, NULL, 3, NULL, 0)
 */
void clock_sync_task(void *param);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_SYNC_H */
```

- [ ] **Step 2: Update main/CMakeLists.txt**

Add `"clock_sync/clock_sync.c"` to the SRCS list and `"clock_sync"` to INCLUDE_DIRS:

```cmake
idf_component_register(
    SRCS
        "main.c"
        "lorawan/lorawan_handler.cpp"
        "sensors/sensor_manager.c"
        "sensors/sht20_driver.c"
        "sensors/sht3x_driver.c"
        "sensors/am2315c_driver.c"
        "sensors/max6675_driver.c"
        "payload/cayenne_lpp.c"
        "interface/buzzer.c"
        "interface/status_led.c"
        "display/oled_display.c"
        "wifi/wifi_manager.c"
        "webserver/web_server.c"
        "webserver/handlers/auth.c"
        "webserver/handlers/static_files.c"
        "webserver/handlers/api_files.c"
        "webserver/handlers/api_system.c"
        "webserver/handlers/api_wifi.c"
        "webserver/handlers/api_lorawan.c"
        "webserver/handlers/api_sensors.c"
        "webserver/handlers/api_ota.c"
        "config/config_manager.c"
        "health/health_monitor.c"
        "logs/log_buffer.c"
        "clock_sync/clock_sync.c"
    INCLUDE_DIRS
        "."
        "lorawan"
        "sensors"
        "payload"
        "display"
        "wifi"
        "webserver"
        "webserver/handlers"
        "config"
        "health"
        "logs"
        "interface"
        "clock_sync"
    REQUIRES
        driver
        nvs_flash
        esp_wifi
        esp_http_server
        json
        mbedtls
        esp_timer
        freertos
        app_update
        esp_http_client
)
```

- [ ] **Step 3: Create main/clock_sync/clock_sync.c**

```c
/**
 * @file clock_sync.c
 * @brief LoRaWAN Application Layer Clock Synchronization (TS003)
 */

#include "clock_sync.h"
#include "lorawan_handler.h"

#include <string.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "CLOCK_SYNC";

// GPS epoch: 1980-01-06 00:00:00 UTC
// Unix epoch: 1970-01-01 00:00:00 UTC
// Difference: 315964800 seconds
#define GPS_TO_UNIX_OFFSET  315964800UL

// Max plausible correction: 1 year in seconds
#define MAX_CORRECTION_S    (365 * 24 * 3600)

// LoRaWAN TS003 constants — defined here to avoid including a C++ header
// Values from managed_components/jgromes__radiolib/src/protocols/LoRaWAN/LoRaWAN.h
#define CLOCK_SYNC_FPORT        202   // RADIOLIB_LORAWAN_FPORT_TS003
#define CLOCK_SYNC_PACKAGE_ID     1   // RADIOLIB_LORAWAN_PACKAGE_TS003

// State
static volatile bool s_synced = false;
static volatile uint32_t s_device_time_sent = 0;

// ============================================================================
// AppTimeAns callback (called by RadioLib inside sendReceive)
// ============================================================================

static void on_apptime_ans(uint8_t *data, size_t len)
{
    if (len < 5) {
        ESP_LOGW(TAG, "AppTimeAns too short: %zu bytes (expected 5)", len);
        return;
    }

    // Parse TimeCorrection (int32, little-endian)
    int32_t correction = 0;
    memcpy(&correction, data, sizeof(int32_t));

    // Validate range
    if (correction > MAX_CORRECTION_S || correction < -MAX_CORRECTION_S) {
        ESP_LOGW(TAG, "TimeCorrection out of range: %ld s — ignoring", (long)correction);
        return;
    }

    // Calculate Unix time:
    //   unix_time = (DeviceTime_sent + correction) + GPS_TO_UNIX_OFFSET
    time_t unix_time = (time_t)((uint32_t)s_device_time_sent + correction) + GPS_TO_UNIX_OFFSET;

    struct timeval tv = { .tv_sec = unix_time, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    s_synced = true;

    struct tm timeinfo;
    gmtime_r(&unix_time, &timeinfo);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "Clock synced to %s UTC (correction: %lds)", time_str, (long)correction);
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t clock_sync_init(void)
{
    s_synced = false;
    s_device_time_sent = 0;
    ESP_LOGI(TAG, "Clock sync module initialized");
    return ESP_OK;
}

esp_err_t clock_sync_request(void)
{
    // Build AppTimeReq (5 bytes)
    // [0-3] DeviceTime (uint32, GPS epoch seconds, little-endian)
    // [4]   Param: bit0=1 (AnsRequired)
    uint8_t req[5];

    // Before first sync, DeviceTime = 0.
    // After sync, convert current Unix time back to GPS epoch.
    uint32_t device_time;
    if (s_synced) {
        time_t now = time(NULL);
        device_time = (uint32_t)((now > (time_t)GPS_TO_UNIX_OFFSET)
                                  ? (now - GPS_TO_UNIX_OFFSET)
                                  : 0);
    } else {
        device_time = 0;
    }

    s_device_time_sent = device_time;

    req[0] = (uint8_t)(device_time & 0xFF);
    req[1] = (uint8_t)((device_time >> 8) & 0xFF);
    req[2] = (uint8_t)((device_time >> 16) & 0xFF);
    req[3] = (uint8_t)((device_time >> 24) & 0xFF);
    req[4] = 0x01;  // AnsRequired = 1

    ESP_LOGI(TAG, "Sending AppTimeReq (DeviceTime=%lu)", (unsigned long)device_time);

    esp_err_t ret = lorawan_send(req, sizeof(req), CLOCK_SYNC_FPORT, false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AppTimeReq send failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

bool clock_sync_is_synced(void)
{
    return s_synced;
}

time_t clock_sync_get_time(void)
{
    if (!s_synced) {
        return 0;
    }
    return time(NULL);
}

void clock_sync_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "Clock sync task started");

    // Wait for LoRaWAN join
    while (!lorawan_is_joined()) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    // Register TS003 callback now that lorawan_init() has run
    esp_err_t ret = lorawan_add_app_package(CLOCK_SYNC_PACKAGE_ID, on_apptime_ans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register TS003 package: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    // Initial sync
    clock_sync_request();

    // Daily re-sync loop (24h = 86400000 ms)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(24UL * 3600UL * 1000UL));
        ESP_LOGI(TAG, "Daily re-sync triggered");
        clock_sync_request();
    }
}
```

- [ ] **Step 4: Build to verify**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build
```

Expected: build succeeds. `clock_sync.c` uses `CLOCK_SYNC_FPORT=202` and `CLOCK_SYNC_PACKAGE_ID=1` as local defines — no C++ RadioLib headers are included from this C file.

- [ ] **Step 5: Commit**

```bash
git add main/clock_sync/clock_sync.h main/clock_sync/clock_sync.c main/CMakeLists.txt
git commit -m "feat(clock_sync): add TS003 app-layer clock sync module"
```

---

## Task 3: Wire up clock_sync in main.c

**Files:**
- Modify: `main/main.c`

- [ ] **Step 1: Add include and clock_sync_init() call**

At the top of `main/main.c`, add after the existing includes:

```c
#include "clock_sync.h"
```

In `app_main()`, add `clock_sync_init()` after `health_monitor_init()` and before spawning tasks (around line 279):

```c
    // Initialize clock sync module
    clock_sync_init();
```

- [ ] **Step 2: Spawn clock_sync_task**

After the existing `xTaskCreatePinnedToCore` calls (after the display task, around line 292), add:

```c
    // Clock sync task - Core 0, Priority 3, Stack 3072
    xTaskCreatePinnedToCore(clock_sync_task, "clock_sync", 3072, NULL, 3, NULL, 0);
```

- [ ] **Step 3: Add clock-aligned uplink scheduling to uplink_task**

In `uplink_task()` (starts at line 154), replace the delay at the top of the while loop:

Current code:
```c
    while (1) {
        uint32_t interval_s = config_get_uplink_interval();
        if (interval_s < 10) interval_s = 10;

        vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));
```

Replace with:
```c
    while (1) {
        uint32_t interval_s = config_get_uplink_interval();
        if (interval_s < 10) interval_s = 10;

        if (clock_sync_is_synced()) {
            // Align to the next clock boundary (e.g., hourly at :00:00)
            time_t now = time(NULL);
            time_t next = ((now / (time_t)interval_s) + 1) * (time_t)interval_s;
            uint32_t delay_ms = (uint32_t)((next - now) * 1000);
            if (delay_ms > interval_s * 2000) {
                // Safety cap: never wait more than 2× the interval
                delay_ms = interval_s * 1000;
            }
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        } else {
            vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));
        }
```

- [ ] **Step 4: Build to verify**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build
```

Expected: build succeeds with no errors.

- [ ] **Step 5: Commit**

```bash
git add main/main.c
git commit -m "feat(main): integrate clock_sync task and aligned uplink scheduling"
```

---

## Task 4: Show time on OLED system page

**Files:**
- Modify: `main/display/oled_display.c`

- [ ] **Step 1: Add includes to oled_display.c**

At the top of `main/display/oled_display.c`, after the existing includes, add:

```c
#include <time.h>
#include "clock_sync.h"
```

- [ ] **Step 2: Replace row 0 in oled_display_show_system()**

In `oled_display_show_system()` (around line 402), replace:

```c
    oled_display_text(0, 0, "--- SYSTEM ---");
```

with:

```c
    if (clock_sync_is_synced()) {
        time_t now = time(NULL);
        struct tm ti;
        gmtime_r(&now, &ti);
        char time_line[22];
        strftime(time_line, sizeof(time_line), "%d/%m %H:%M:%S UTC", &ti);
        oled_display_text(0, 0, time_line);
    } else {
        oled_display_text(0, 0, "--/-- --:--:--");
    }
```

- [ ] **Step 3: Build to verify**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add main/display/oled_display.c
git commit -m "feat(oled): show synchronized time on system page"
```

---

## Task 5: Enable real-time log timestamps

**Files:**
- Modify: `sdkconfig.defaults`

- [ ] **Step 1: Add log timestamp config to sdkconfig.defaults**

In `sdkconfig.defaults`, add after the `# Log level` section:

```
# Use system time (real UTC) for log timestamps after clock sync
CONFIG_LOG_TIMESTAMP_SOURCE_SYSTEM=y
```

- [ ] **Step 2: Apply the config change**

`sdkconfig.defaults` only applies when `sdkconfig` doesn't exist. Delete `sdkconfig` and rebuild:

```bash
rm sdkconfig
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && idf.py build
```

Expected: build succeeds. After flashing and clock sync, log lines will show `H:MM:SS.mmm` format instead of uptime milliseconds.

- [ ] **Step 3: Commit**

```bash
git add sdkconfig.defaults
git commit -m "feat(config): enable system-time log timestamps for TS003 clock sync"
```

---

## Verification Checklist (after flashing)

- [ ] Device joins LoRaWAN network
- [ ] After join, clock_sync_task logs `"Sending AppTimeReq"` on FPort 202
- [ ] ChirpStack shows the AppTimeReq uplink on FPort 202 in the device live-data
- [ ] If ChirpStack has TS003 enabled, device logs `"Clock synced to YYYY-MM-DD HH:MM:SS UTC"`
- [ ] OLED system page shows `"DD/MM HH:MM:SS UTC"` instead of `"--- SYSTEM ---"`
- [ ] Log timestamps switch from uptime to real time after sync
- [ ] Uplink intervals align to clock boundaries (e.g., at :00:00 for 3600s interval)
