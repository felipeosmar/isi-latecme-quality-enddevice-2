# Multi-Hardware Variants Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a layered sdkconfig feature-file system with a SKU manifest in `build.sh`, supporting OLED-less, WiFi-less, and 2MB-flash hardware variants.

**Architecture:** Each hardware dimension gets its own `sdkconfig.defaults.FEATURE` file. `build.sh` defines named SKUs as ordered combinations of these files. Feature code in C is gated by `CONFIG_OLED_ENABLED` and `CONFIG_WIFI_ENABLED` Kconfig booleans, following the same pattern already used for `CONFIG_THERMOCOUPLE_ENABLED`.

**Tech Stack:** ESP-IDF v5.5.3, Kconfig, CMake, bash, GitHub Actions

**Spec:** `docs/superpowers/specs/2026-04-22-multi-hardware-variants-design.md`

---

## File Map

| File | Action | Why |
|---|---|---|
| `sdkconfig.defaults.salt_spray` | Delete | Renamed to `thermocouple` |
| `sdkconfig.defaults.thermocouple` | Create | Enables MAX6675, replaces salt_spray |
| `sdkconfig.defaults.2mb` | Create | 2MB flash: no OTA, factory partition |
| `sdkconfig.defaults.no_oled` | Create | Disables OLED feature |
| `sdkconfig.defaults.no_wifi` | Create | Disables WiFi + web server feature |
| `partitions.2mb.csv` | Create | Partition layout for 2MB devices |
| `main/Kconfig.projbuild` | Modify | Add OLED_ENABLED and WIFI_ENABLED |
| `main/CMakeLists.txt` | Modify | Conditional srcs + REQUIRES |
| `main/health/health_monitor.c` | Modify | Guard wifi_manager calls |
| `main/display/oled_display.c` | Modify | Guard clock_sync call |
| `main/main.c` | Modify | Guard includes, functions, init sequence |
| `build.sh` | Modify | Add SKU manifest + build command |
| `.github/workflows/release.yml` | Modify | Matrix build per SKU |

---

## Task 1: Rename salt_spray → thermocouple

**Files:**
- Delete: `sdkconfig.defaults.salt_spray`
- Create: `sdkconfig.defaults.thermocouple`
- Modify: `.github/workflows/release.yml` (reference update only)

- [ ] **Step 1: Delete old file and create renamed file**

```bash
rm sdkconfig.defaults.salt_spray
```

Create `sdkconfig.defaults.thermocouple` with this exact content:

```ini
CONFIG_THERMOCOUPLE_ENABLED=y
```

- [ ] **Step 2: Update release.yml — rename the salt_spray job and binary**

In `.github/workflows/release.yml`, make these changes:

Change job name `build-salt-spray` → `build-jvtech-4mb-thermocouple`.

Change build step:
```yaml
      - name: Build salt spray variant
        run: |
          . $IDF_PATH/export.sh
          rm -f sdkconfig
          idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.salt_spray" build
```
→
```yaml
      - name: Build jvtech_4mb_thermocouple variant
        run: |
          . $IDF_PATH/export.sh
          rm -f sdkconfig
          idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.thermocouple" build
```

Change artifact name `firmware-salt-spray` → `firmware-jvtech-4mb-thermocouple` (both in upload and download steps).

Change binary rename line:
```bash
mv artifacts/firmware-salt-spray/lorawan-enddevice.bin artifacts/lorawan-enddevice-${TAG}-salt_spray.bin
```
→
```bash
mv artifacts/firmware-jvtech-4mb-thermocouple/lorawan-enddevice.bin artifacts/lorawan-enddevice-${TAG}-jvtech_4mb_thermocouple.bin
```

Change the release files list to use `lorawan-enddevice-${{ steps.release_info.outputs.tag }}-jvtech_4mb_thermocouple.bin`.

- [ ] **Step 3: Commit**

```bash
git add sdkconfig.defaults.thermocouple .github/workflows/release.yml
git rm sdkconfig.defaults.salt_spray
git commit -m "refactor: rename salt_spray variant to thermocouple"
```

---

## Task 2: Create new config and partition files

**Files:**
- Create: `partitions.2mb.csv`
- Create: `sdkconfig.defaults.2mb`
- Create: `sdkconfig.defaults.no_oled`
- Create: `sdkconfig.defaults.no_wifi`

- [ ] **Step 1: Create `partitions.2mb.csv`**

```
# Name,   Type, SubType,  Offset,   Size
nvs,      data, nvs,      0x9000,   0x6000
phy_init, data, phy,      0xf000,   0x1000
factory,  app,  factory,  0x20000,  0x140000
coredump, data, coredump, 0x160000, 0x10000
www,      data, spiffs,   0x170000, 0x30000
userdata, data, spiffs,   0x1A0000, 0x10000
```

Total: ~1.69MB < 2MB. Single factory partition — no OTA slots, reflash only.

- [ ] **Step 2: Create `sdkconfig.defaults.2mb`**

```ini
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.2mb.csv"
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=n
```

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=n` is required: the base enables it, but the 2MB layout has no OTA slots — the bootloader would crash at boot trying to find them.

- [ ] **Step 3: Create `sdkconfig.defaults.no_oled`**

```ini
CONFIG_OLED_ENABLED=n
```

- [ ] **Step 4: Create `sdkconfig.defaults.no_wifi`**

```ini
CONFIG_WIFI_ENABLED=n
CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n
```

- [ ] **Step 5: Commit**

```bash
git add partitions.2mb.csv sdkconfig.defaults.2mb sdkconfig.defaults.no_oled sdkconfig.defaults.no_wifi
git commit -m "feat: add 2MB partition layout and feature sdkconfig layers"
```

---

## Task 3: Add OLED_ENABLED and WIFI_ENABLED to Kconfig

**Files:**
- Modify: `main/Kconfig.projbuild`

Current file:

```kconfig
menu "Firmware Variant"

config THERMOCOUPLE_ENABLED
    bool "Enable MAX6675 thermocouple sensor"
    default n
    help
        Enables the MAX6675 SPI thermocouple driver, thermocouple config
        fields, and thermocouple alarm thresholds. Disable for the standard
        (temperature + humidity only) variant.

endmenu
```

- [ ] **Step 1: Add OLED_ENABLED and WIFI_ENABLED entries**

Replace the entire file with:

```kconfig
menu "Firmware Variant"

config THERMOCOUPLE_ENABLED
    bool "Enable MAX6675 thermocouple sensor"
    default n
    help
        Enables the MAX6675 SPI thermocouple driver, thermocouple config
        fields, and thermocouple alarm thresholds. Disable for the standard
        (temperature + humidity only) variant.

config OLED_ENABLED
    bool "Enable OLED display"
    default y
    help
        Enables the SSD1306 OLED display driver and display task.
        Disable for headless hardware variants.

config WIFI_ENABLED
    bool "Enable WiFi, web server and OTA"
    default y
    help
        Enables WiFi manager, HTTP web server, auto-updater, and NTP clock sync.
        Disable for variants without network connectivity.

endmenu
```

- [ ] **Step 2: Verify Kconfig parses correctly**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh
idf.py build 2>&1 | head -30
```

Expected: build starts normally (no Kconfig parse errors). Let it run a few seconds then Ctrl+C if you don't want the full build.

- [ ] **Step 3: Commit**

```bash
git add main/Kconfig.projbuild
git commit -m "feat(kconfig): add OLED_ENABLED and WIFI_ENABLED variant flags"
```

---

## Task 4: Update CMakeLists.txt — conditional sources and REQUIRES

**Files:**
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Replace CMakeLists.txt with conditional source and REQUIRES lists**

```cmake
set(srcs
    "main.c"
    "lorawan/lorawan_handler.cpp"
    "sensors/sensor_manager.c"
    "sensors/sht20_driver.c"
    "sensors/sht3x_driver.c"
    "sensors/am2315c_driver.c"
    "payload/cayenne_lpp.c"
    "interface/buzzer.c"
    "interface/status_led.c"
    "config/config_manager.c"
    "health/health_monitor.c"
    "logs/log_buffer.c"
    "alarm/alarm_manager.c"
    "interface/button_handler.c"
)

if(CONFIG_THERMOCOUPLE_ENABLED)
    list(APPEND srcs "sensors/max6675_driver.c")
endif()

if(CONFIG_OLED_ENABLED)
    list(APPEND srcs "display/oled_display.c")
endif()

if(CONFIG_WIFI_ENABLED)
    list(APPEND srcs
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
        "update/auto_updater.c"
        "clock_sync/clock_sync.c"
    )
endif()

set(requires
    driver
    nvs_flash
    json
    mbedtls
    esp_timer
    freertos
)

if(CONFIG_WIFI_ENABLED)
    list(APPEND requires
        esp_wifi
        esp_http_server
        app_update
        esp_http_client
    )
endif()

idf_component_register(
    SRCS ${srcs}
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
        "alarm"
        "update"
    REQUIRES ${requires}
)
```

Note: `INCLUDE_DIRS` keeps all paths unconditional — `health_monitor.c` (always compiled) still includes `wifi_manager.h` via a `#if` guard and needs the `wifi` include dir to find the header.

- [ ] **Step 2: Commit**

```bash
git add main/CMakeLists.txt
git commit -m "feat(cmake): conditional compilation for OLED and WiFi features"
```

---

## Task 5: Guard indirect WiFi dependencies in health_monitor.c and oled_display.c

Two always-compiled files reference WiFi/clock_sync symbols. Both need `#if` guards.

**Files:**
- Modify: `main/health/health_monitor.c`
- Modify: `main/display/oled_display.c`

- [ ] **Step 1: Guard wifi_manager in health_monitor.c**

Find line `#include "wifi_manager.h"` and wrap it:

```c
#if CONFIG_WIFI_ENABLED
#include "wifi_manager.h"
#endif
```

Find line:
```c
            s_health.wifi_connected = wifi_manager_is_connected();
```
Wrap it:
```c
#if CONFIG_WIFI_ENABLED
            s_health.wifi_connected = wifi_manager_is_connected();
#endif
```

- [ ] **Step 2: Guard clock_sync in oled_display.c**

Find line `#include "clock_sync.h"` and wrap it:

```c
#if CONFIG_WIFI_ENABLED
#include "clock_sync.h"
#endif
```

Find the block in `oled_display_show_system()` at line ~444:

```c
    if (clock_sync_is_synced()) {
        time_t now = time(NULL);
        struct tm ti;
        gmtime_r(&now, &ti);
        char time_line[22];
        strftime(time_line, sizeof(time_line), "%d/%m %H:%M:%S UTC", &ti);
        oled_display_text(0, 0, time_line);
    } else {
        oled_display_text(0, 0, "--/-- --:--:-- UTC");
    }
```

Replace with:

```c
#if CONFIG_WIFI_ENABLED
    if (clock_sync_is_synced()) {
        time_t now = time(NULL);
        struct tm ti;
        gmtime_r(&now, &ti);
        char time_line[22];
        strftime(time_line, sizeof(time_line), "%d/%m %H:%M:%S UTC", &ti);
        oled_display_text(0, 0, time_line);
    } else {
        oled_display_text(0, 0, "--/-- --:--:-- UTC");
    }
#else
    oled_display_text(0, 0, "--/-- --:--:-- UTC");
#endif
```

- [ ] **Step 3: Commit**

```bash
git add main/health/health_monitor.c main/display/oled_display.c
git commit -m "feat: guard wifi/clock_sync deps in health_monitor and oled_display"
```

---

## Task 6: Update main.c with #if guards

**Files:**
- Modify: `main/main.c`

Four changes needed:
1. Guard `#include` directives for WiFi/OLED/clock_sync headers
2. Guard the `init_wifi()` static function definition
3. Guard the `display_task()` static function (whole function + the `wifi_manager_get_ip` call inside it)
4. Guard `clock_sync_is_synced()` in `uplink_task`
5. Guard the WiFi/web/clock/OTA init sequence in `app_main`

- [ ] **Step 1: Guard includes at top of file**

Replace the include block (lines 25–39):

```c
#include "wifi_manager.h"
#include "web_server.h"
#include "config_manager.h"
#include "health_monitor.h"
#include "log_buffer.h"
#include "sensor_manager.h"
#include "lorawan_handler.h"
#include "oled_display.h"
#include "cayenne_lpp.h"
#include "buzzer.h"
#include "status_led.h"
#include "clock_sync.h"
#include "alarm_manager.h"
#include "button_handler.h"
#include "auto_updater.h"
```

With:

```c
#include "config_manager.h"
#include "health_monitor.h"
#include "log_buffer.h"
#include "sensor_manager.h"
#include "lorawan_handler.h"
#include "cayenne_lpp.h"
#include "buzzer.h"
#include "status_led.h"
#include "alarm_manager.h"
#include "button_handler.h"

#if CONFIG_WIFI_ENABLED
#include "wifi_manager.h"
#include "web_server.h"
#include "clock_sync.h"
#include "auto_updater.h"
#endif

#if CONFIG_OLED_ENABLED
#include "oled_display.h"
#endif
```

- [ ] **Step 2: Guard init_wifi() function**

Wrap the entire `static esp_err_t init_wifi(void)` function (lines 46–89) with:

```c
#if CONFIG_WIFI_ENABLED
static esp_err_t init_wifi(void)
{
    // ... existing body unchanged ...
}
#endif
```

- [ ] **Step 3: Guard display_task() function and its internal wifi call**

Wrap the entire `static void display_task(void *param)` function with `#if CONFIG_OLED_ENABLED`. Inside the function, guard the `wifi_manager_get_ip` call:

```c
#if CONFIG_OLED_ENABLED
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
            if (alarm_manager_get_active_info(&info)) {
                oled_display_show_alarm(&info);
            }
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
#if CONFIG_WIFI_ENABLED
                    wifi_manager_get_ip(ip);
#endif
                    uint32_t uptime_s  = xTaskGetTickCount() / configTICK_RATE_HZ;
                    lorawan_stats_t lora_stats;
                    lorawan_get_stats(&lora_stats);
                    oled_display_show_system(ip, uptime_s,
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
#endif
```

- [ ] **Step 4: Guard clock_sync_is_synced() in uplink_task**

Find in `uplink_task`:

```c
        if (clock_sync_is_synced()) {
```

Replace with:

```c
#if CONFIG_WIFI_ENABLED
        if (clock_sync_is_synced()) {
            // Align to the next clock boundary (e.g., hourly at :00:00)
            time_t now = time(NULL);
            time_t next = ((now / (time_t)interval_s) + 1) * (time_t)interval_s;
            // next - now is always in (0, interval_s] by the floor-division formula
            uint32_t delay_ms = (uint32_t)((next - now) * 1000);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        } else {
            vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));
        }
#else
        vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));
#endif
```

(Remove the original `if (clock_sync_is_synced()) { ... } else { ... }` block and replace with the above.)

- [ ] **Step 5: Guard WiFi/web/clock/OTA init in app_main**

In `app_main`, replace the WiFi init block (from "Initialize WiFi" comment through the clock_sync_init, auto_updater_init, and the display/clock task creation):

```c
    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    if (init_wifi() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi!");
        return;
    }

    // Wait for WiFi to be ready
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Start web server
    ESP_LOGI(TAG, "Starting web server...");
    web_server_config_t web_cfg = {
        .port = 80,
        .username = config_get_web_username(),
        .password = config_get_web_password(),
        .auth_enabled = config_get_web_auth_enabled(),
    };

    if (web_server_init(&web_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server!");
        return;
    }

    // Print access information
    char ip[16];
    if (wifi_manager_get_ip(ip) == ESP_OK) {
        ESP_LOGI(TAG, "==========================================");
        ESP_LOGI(TAG, "  Web Interface: http://%s", ip);
        ESP_LOGI(TAG, "==========================================");
    }
```

Replace with:

```c
#if CONFIG_WIFI_ENABLED
    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    if (init_wifi() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi!");
        return;
    }

    // Wait for WiFi to be ready
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Start web server
    ESP_LOGI(TAG, "Starting web server...");
    web_server_config_t web_cfg = {
        .port = 80,
        .username = config_get_web_username(),
        .password = config_get_web_password(),
        .auth_enabled = config_get_web_auth_enabled(),
    };

    if (web_server_init(&web_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server!");
        return;
    }

    // Print access information
    char ip[16];
    if (wifi_manager_get_ip(ip) == ESP_OK) {
        ESP_LOGI(TAG, "==========================================");
        ESP_LOGI(TAG, "  Web Interface: http://%s", ip);
        ESP_LOGI(TAG, "==========================================");
    }
#endif
```

And replace the task creation block at the end of `app_main`:

```c
    // Display task - Core 0, Priority 3, Stack 4096
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 3, NULL, 0);

    // Clock sync task - Core 0, Priority 3, Stack 4096
    xTaskCreatePinnedToCore(clock_sync_task, "clock_sync", 4096, NULL, 3, NULL, 0);

    // Auto-updater task - Core 0, Priority 3, Stack 8KB
    if (auto_updater_init() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start auto-updater");
    }
```

With:

```c
#if CONFIG_OLED_ENABLED
    // Display task - Core 0, Priority 3, Stack 4096
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 3, NULL, 0);
#endif

#if CONFIG_WIFI_ENABLED
    // Clock sync task - Core 0, Priority 3, Stack 4096
    xTaskCreatePinnedToCore(clock_sync_task, "clock_sync", 4096, NULL, 3, NULL, 0);

    // Auto-updater task - Core 0, Priority 3, Stack 8KB
    if (auto_updater_init() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start auto-updater");
    }
#endif
```

Also remove the `clock_sync_init()` call from `app_main`:

```c
    // Initialize clock sync module
    clock_sync_init();
```

Replace with:

```c
#if CONFIG_WIFI_ENABLED
    // Initialize clock sync module
    clock_sync_init();
#endif
```

- [ ] **Step 6: Commit**

```bash
git add main/main.c
git commit -m "feat: add CONFIG_OLED_ENABLED and CONFIG_WIFI_ENABLED guards to main.c"
```

---

## Task 7: Build and verify all SKUs compile

No unit test suite exists. Verification is build-time only.

**Prerequisite:** ESP-IDF environment must be sourced: `. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh`

- [ ] **Step 1: Verify jvtech_4mb_standard (regression — current behavior)**

```bash
rm -f sdkconfig
idf.py build
```

Expected: `Project build complete.` — same as before.

- [ ] **Step 2: Verify jvtech_4mb_thermocouple (renamed from salt_spray)**

```bash
rm -f sdkconfig
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.thermocouple" build
```

Expected: `Project build complete.` with `CONFIG_THERMOCOUPLE_ENABLED=y` visible in build output.

- [ ] **Step 3: Verify jvtech_2mb_standard**

```bash
rm -f sdkconfig
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.2mb" build
```

Expected: `Project build complete.`

- [ ] **Step 4: Verify jvtech_2mb_headless (no OLED, no WiFi)**

```bash
rm -f sdkconfig
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.2mb;sdkconfig.defaults.no_oled;sdkconfig.defaults.no_wifi" build
```

Expected: `Project build complete.`

- [ ] **Step 5: Restore default sdkconfig for local development**

```bash
rm -f sdkconfig
idf.py build
```

- [ ] **Step 6: Commit if any minor fixes were needed during build verification**

```bash
git add -p
git commit -m "fix: resolve compilation issues found during multi-SKU build verification"
```

---

## Task 8: Redesign build.sh with SKU manifest

**Files:**
- Modify: `build.sh`

- [ ] **Step 1: Replace build.sh**

```bash
#!/bin/bash
# Build and flash helper for lorawan-enddevice
# Manages hardware SKU variants via layered sdkconfig.defaults files

. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh >/dev/null

set -e

PORT="${PORT:-/dev/ttyUSB0}"
BAUD="${BAUD:-460800}"

# ── SKU Manifest ──────────────────────────────────────────────────────────────
# Single source of truth for hardware variant combinations.
# To add a new SKU: add one entry here, add the SKU to the CI matrix in release.yml.
declare -A SKU_DEFAULTS=(
  [jvtech_4mb_standard]="sdkconfig.defaults"
  [jvtech_4mb_thermocouple]="sdkconfig.defaults;sdkconfig.defaults.thermocouple"
  [jvtech_2mb_standard]="sdkconfig.defaults;sdkconfig.defaults.2mb"
  [jvtech_2mb_headless]="sdkconfig.defaults;sdkconfig.defaults.2mb;sdkconfig.defaults.no_oled;sdkconfig.defaults.no_wifi"
)
DEFAULT_SKU="jvtech_4mb_standard"
# ──────────────────────────────────────────────────────────────────────────────

show_help() {
    echo "Usage: $0 <command> [options]"
    echo ""
    echo "Build commands:"
    echo "  build [SKU]         Compile firmware for a SKU (default: $DEFAULT_SKU)"
    echo "  skus                List all available SKUs"
    echo ""
    echo "Flash commands:"
    echo "  all                 Full flash (factory reset) - erases user config"
    echo "  app                 Flash only firmware (preserves www and userdata)"
    echo "  update              Flash firmware + web interface (preserves userdata/config)"
    echo "  www                 Flash only web interface"
    echo ""
    echo "Environment variables:"
    echo "  PORT                Serial port (default: /dev/ttyUSB0)"
    echo "  BAUD                Baud rate (default: 460800)"
    echo ""
    echo "Examples:"
    echo "  $0 build                          # Build default SKU"
    echo "  $0 build jvtech_2mb_headless      # Build specific SKU"
    echo "  $0 skus                           # List all SKUs"
    echo "  $0 update                         # Flash built firmware (keeps config)"
    echo "  $0 all                            # Factory reset flash"
    echo "  PORT=/dev/ttyACM0 $0 app          # Flash app to specific port"
}

list_skus() {
    echo "Available SKUs:"
    for sku in "${!SKU_DEFAULTS[@]}"; do
        echo "  $sku"
        echo "    layers: ${SKU_DEFAULTS[$sku]}"
    done
}

build_sku() {
    local sku="${1:-$DEFAULT_SKU}"

    if [[ -z "${SKU_DEFAULTS[$sku]}" ]]; then
        echo "Error: Unknown SKU '$sku'"
        echo ""
        list_skus
        exit 1
    fi

    local defaults="${SKU_DEFAULTS[$sku]}"
    echo "=== Building SKU: $sku ==="
    echo "    Layers: $defaults"
    rm -f sdkconfig
    idf.py -DSDKCONFIG_DEFAULTS="$defaults" build
    echo "=== Build complete: $sku ==="
}

check_build() {
    if [ ! -f "build/lorawan-enddevice.bin" ]; then
        echo "Error: Build not found. Run '$0 build' first."
        exit 1
    fi
}

flash_all() {
    echo "=== Full Flash (Factory Reset) ==="
    echo "WARNING: This will erase ALL user configuration!"
    read -p "Continue? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        idf.py -p "$PORT" -b "$BAUD" flash
    fi
}

flash_app() {
    echo "=== Flashing Firmware Only ==="
    check_build
    idf.py -p "$PORT" -b "$BAUD" app-flash
}

flash_www() {
    echo "=== Flashing Web Interface Only ==="
    if [ ! -f "build/www.bin" ]; then
        echo "Error: www.bin not found. Run '$0 build' first."
        exit 1
    fi
    esptool.py -p "$PORT" -b "$BAUD" write_flash 0x370000 build/www.bin
}

flash_update() {
    echo "=== Update Flash (Firmware + Web Interface) ==="
    echo "User configuration will be PRESERVED"
    check_build

    idf.py -p "$PORT" -b "$BAUD" app-flash

    if [ -f "build/www.bin" ]; then
        echo "Flashing web interface..."
        esptool.py -p "$PORT" -b "$BAUD" write_flash 0x370000 build/www.bin
    fi

    echo "=== Update Complete ==="
}

case "$1" in
    build)
        build_sku "$2"
        ;;
    skus)
        list_skus
        ;;
    all)
        flash_all
        ;;
    app)
        flash_app
        ;;
    www)
        flash_www
        ;;
    update)
        flash_update
        ;;
    -h|--help|help)
        show_help
        ;;
    *)
        show_help
        exit 1
        ;;
esac
```

- [ ] **Step 2: Make executable and commit**

```bash
chmod +x build.sh
git add build.sh
git commit -m "feat(build): redesign build.sh with SKU manifest and build command"
```

---

## Task 9: Update release.yml with matrix build per SKU

**Files:**
- Modify: `.github/workflows/release.yml`

- [ ] **Step 1: Replace release.yml**

```yaml
name: Release

on:
  workflow_dispatch:
  push:
    branches:
      - main

jobs:
  build:
    runs-on: ubuntu-latest
    container:
      image: espressif/idf:v5.5.3
    strategy:
      matrix:
        sku:
          - jvtech_4mb_standard
          - jvtech_4mb_thermocouple
          - jvtech_2mb_standard
          - jvtech_2mb_headless
    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          ref: main

      - name: Build ${{ matrix.sku }}
        run: |
          . $IDF_PATH/export.sh
          ./build.sh build ${{ matrix.sku }}

      - name: Upload ${{ matrix.sku }} artifacts
        uses: actions/upload-artifact@v4
        with:
          name: firmware-${{ matrix.sku }}
          path: |
            build/lorawan-enddevice.bin
            build/bootloader/bootloader.bin
            build/partition_table/partition-table.bin
            build/www.bin
          if-no-files-found: warn
          retention-days: 1

  release:
    needs: build
    runs-on: ubuntu-latest
    permissions:
      contents: write
    steps:
      - name: Calculate release number
        id: release_info
        run: |
          COUNT=$(gh api repos/${{ github.repository }}/releases \
            --paginate \
            --jq '.[] | select(.tag_name | test("^main-r[0-9]+$")) | .tag_name' \
            2>/dev/null | wc -l || echo 0)
          NEXT=$((COUNT + 1))
          echo "tag=main-r${NEXT}" >> $GITHUB_OUTPUT
          echo "name=Release #${NEXT}" >> $GITHUB_OUTPUT
        env:
          GH_TOKEN: ${{ github.token }}

      - name: Download all artifacts
        uses: actions/download-artifact@v4
        with:
          path: artifacts/

      - name: Rename binaries with tag and SKU
        run: |
          TAG="${{ steps.release_info.outputs.tag }}"
          for sku in jvtech_4mb_standard jvtech_4mb_thermocouple jvtech_2mb_standard jvtech_2mb_headless; do
            src="artifacts/firmware-${sku}/lorawan-enddevice.bin"
            if [ -f "$src" ]; then
              mv "$src" "artifacts/lorawan-enddevice-${TAG}-${sku}.bin"
            fi
          done
          # Shared artifacts from standard build
          mv artifacts/firmware-jvtech_4mb_standard/bootloader/bootloader.bin \
             "artifacts/bootloader-${TAG}.bin" 2>/dev/null || true
          mv artifacts/firmware-jvtech_4mb_standard/partition_table/partition-table.bin \
             "artifacts/partition-table-4mb-${TAG}.bin" 2>/dev/null || true
          mv artifacts/firmware-jvtech_2mb_standard/partition_table/partition-table.bin \
             "artifacts/partition-table-2mb-${TAG}.bin" 2>/dev/null || true
          mv artifacts/firmware-jvtech_4mb_standard/www.bin \
             "artifacts/www-${TAG}.bin" 2>/dev/null || true

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          tag_name: ${{ steps.release_info.outputs.tag }}
          name: ${{ steps.release_info.outputs.name }}
          prerelease: false
          generate_release_notes: true
          files: |
            artifacts/lorawan-enddevice-${{ steps.release_info.outputs.tag }}-jvtech_4mb_standard.bin
            artifacts/lorawan-enddevice-${{ steps.release_info.outputs.tag }}-jvtech_4mb_thermocouple.bin
            artifacts/lorawan-enddevice-${{ steps.release_info.outputs.tag }}-jvtech_2mb_standard.bin
            artifacts/lorawan-enddevice-${{ steps.release_info.outputs.tag }}-jvtech_2mb_headless.bin
            artifacts/www-${{ steps.release_info.outputs.tag }}.bin
            artifacts/bootloader-${{ steps.release_info.outputs.tag }}.bin
            artifacts/partition-table-4mb-${{ steps.release_info.outputs.tag }}.bin
            artifacts/partition-table-2mb-${{ steps.release_info.outputs.tag }}.bin
```

- [ ] **Step 2: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: matrix build per hardware SKU in release workflow"
```

---

## Adding a New SKU in the Future

1. Create any new `sdkconfig.defaults.FEATURE` files needed.
2. Add one entry to `SKU_DEFAULTS` in `build.sh`.
3. Add the SKU name to the `matrix.sku` list in `.github/workflows/release.yml`.
4. Add the binary to the `files:` list in the release step.
