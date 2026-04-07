# Kconfig Variant Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unify the `salt_spray` and `main` firmware variants into a single codebase controlled by `CONFIG_THERMOCOUPLE_ENABLED`, eliminating branch divergence.

**Architecture:** All changes are made on `salt_spray_dev` (current branch). The `salt_spray` code is the unified base; the standard variant is the same code with `CONFIG_THERMOCOUPLE_ENABLED=n`. After verifying both builds, `main` and `main_dev` are reset to match `salt_spray_dev`/`salt_spray`. Branch protection is then configured on `main`.

**Tech Stack:** ESP-IDF v5.5.3, CMake, Kconfig, GitHub Actions, `gh` CLI.

---

### Task 1: Add Kconfig option and sdkconfig overlay

**Files:**
- Create: `main/Kconfig.projbuild`
- Create: `sdkconfig.defaults.salt_spray`

- [ ] **Step 1: Create `main/Kconfig.projbuild`**

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

- [ ] **Step 2: Create `sdkconfig.defaults.salt_spray`**

```
CONFIG_THERMOCOUPLE_ENABLED=y
```

- [ ] **Step 3: Commit**

```bash
git add main/Kconfig.projbuild sdkconfig.defaults.salt_spray
git commit -m "feat(kconfig): add THERMOCOUPLE_ENABLED variant option"
```

---

### Task 2: Update CMakeLists.txt to conditionally compile max6675_driver.c

**Files:**
- Modify: `main/CMakeLists.txt`

Current file uses `idf_component_register(SRCS "main.c" ... "sensors/max6675_driver.c" ...)` with all sources in one call. Refactor to extract SRCS into a variable so the conditional append works.

- [ ] **Step 1: Rewrite `main/CMakeLists.txt`**

Replace the entire file content with:

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
    "alarm/alarm_manager.c"
    "interface/button_handler.c"
    "update/auto_updater.c"
)

if(CONFIG_THERMOCOUPLE_ENABLED)
    list(APPEND srcs "sensors/max6675_driver.c")
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

- [ ] **Step 2: Commit**

```bash
git add main/CMakeLists.txt
git commit -m "build: conditionally compile max6675_driver based on THERMOCOUPLE_ENABLED"
```

---

### Task 3: Guard MAX6675 code in sensor_manager.c

**Files:**
- Modify: `main/sensors/sensor_manager.c` (lines 15, 132–146, 188–201)

The struct fields (`thermocouple_temp`, `thermocouple_valid`) remain unconditional — they stay `0`/`false` when the driver doesn't initialize, and all downstream code already guards on `thermocouple_valid`. Only the include and the blocks that call driver functions need guards.

- [ ] **Step 1: Wrap the max6675 include (line 15)**

Find:
```c
#include "max6675_driver.h"
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
#include "max6675_driver.h"
#endif
```

- [ ] **Step 2: Wrap the MAX6675 init block (lines 132–146)**

Find:
```c
    // Initialize MAX6675 thermocouple if enabled
    if (config_get_thermocouple_enabled()) {
        int sck = config_get_thermocouple_sck_pin();
        int so = config_get_thermocouple_so_pin();
        int cs = config_get_thermocouple_cs_pin();
        ESP_LOGI(TAG, "Initializing MAX6675 thermocouple (SCK=%d, SO=%d, CS=%d)...", sck, so, cs);
        esp_err_t tc_ret = max6675_init(sck, so, cs);
        if (tc_ret == ESP_OK) {
            ESP_LOGI(TAG, "MAX6675 initialized successfully");
        } else {
            ESP_LOGW(TAG, "MAX6675 init failed: %s (continuing without thermocouple)", esp_err_to_name(tc_ret));
        }
    } else {
        ESP_LOGI(TAG, "Thermocouple disabled in config, skipping MAX6675 init");
    }
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    // Initialize MAX6675 thermocouple if enabled
    if (config_get_thermocouple_enabled()) {
        int sck = config_get_thermocouple_sck_pin();
        int so = config_get_thermocouple_so_pin();
        int cs = config_get_thermocouple_cs_pin();
        ESP_LOGI(TAG, "Initializing MAX6675 thermocouple (SCK=%d, SO=%d, CS=%d)...", sck, so, cs);
        esp_err_t tc_ret = max6675_init(sck, so, cs);
        if (tc_ret == ESP_OK) {
            ESP_LOGI(TAG, "MAX6675 initialized successfully");
        } else {
            ESP_LOGW(TAG, "MAX6675 init failed: %s (continuing without thermocouple)", esp_err_to_name(tc_ret));
        }
    }
#endif
```

- [ ] **Step 3: Wrap the MAX6675 read block (lines 188–201)**

Find:
```c
    // Read MAX6675 thermocouple
    if (config_get_thermocouple_enabled()) {
        esp_err_t ret = max6675_read(&tc_temp);
        if (ret == ESP_OK) {
            tc_temp += config_get_thermocouple_correction();
            float min_temp = config_get_thermocouple_min_temp();
            float max_temp = config_get_thermocouple_max_temp();
            if (tc_temp < min_temp) tc_temp = min_temp;
            if (tc_temp > max_temp) tc_temp = max_temp;
            tc_valid = true;
        } else {
            ESP_LOGW(TAG, "MAX6675 read failed: %s", esp_err_to_name(ret));
        }
    }
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    // Read MAX6675 thermocouple
    if (config_get_thermocouple_enabled()) {
        esp_err_t ret = max6675_read(&tc_temp);
        if (ret == ESP_OK) {
            tc_temp += config_get_thermocouple_correction();
            float min_temp = config_get_thermocouple_min_temp();
            float max_temp = config_get_thermocouple_max_temp();
            if (tc_temp < min_temp) tc_temp = min_temp;
            if (tc_temp > max_temp) tc_temp = max_temp;
            tc_valid = true;
        } else {
            ESP_LOGW(TAG, "MAX6675 read failed: %s", esp_err_to_name(ret));
        }
    }
#endif
```

- [ ] **Step 4: Commit**

```bash
git add main/sensors/sensor_manager.c
git commit -m "feat(sensor_manager): guard MAX6675 driver behind CONFIG_THERMOCOUPLE_ENABLED"
```

---

### Task 4: Guard thermocouple fields in config_manager.c

**Files:**
- Modify: `main/config/config_manager.c` (struct fields, defaults, JSON save/load, getters/setters)

Strategy: keep all function declarations in `config_manager.h` unchanged (no `#ifdef` in the header). In `.c`, guard struct fields, defaults, and JSON I/O. Getter/setter bodies return safe defaults when disabled. This means zero changes needed in call sites (sensor_manager, alarm_manager, etc.).

- [ ] **Step 1: Wrap thermocouple struct fields (lines 40–46)**

Find:
```c
    bool thermocouple_enabled; // MAX6675 thermocouple
    float thermocouple_max_temp; // max temperature for thermocouple
    uint8_t thermocouple_sck_pin; // SPI clock pin
    uint8_t thermocouple_so_pin;  // SPI data out pin
    uint8_t thermocouple_cs_pin;  // SPI chip select pin
    float thermocouple_min_temp;  // min temperature for thermocouple
    float thermocouple_correction; // temperature correction offset
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    bool thermocouple_enabled; // MAX6675 thermocouple
    float thermocouple_max_temp; // max temperature for thermocouple
    uint8_t thermocouple_sck_pin; // SPI clock pin
    uint8_t thermocouple_so_pin;  // SPI data out pin
    uint8_t thermocouple_cs_pin;  // SPI chip select pin
    float thermocouple_min_temp;  // min temperature for thermocouple
    float thermocouple_correction; // temperature correction offset
#endif
```

- [ ] **Step 2: Wrap thermocouple alarm struct fields (lines 72–74)**

Find:
```c
    bool  alarm_tc_enabled;
    float alarm_tc_low;
    float alarm_tc_high;
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    bool  alarm_tc_enabled;
    float alarm_tc_low;
    float alarm_tc_high;
#endif
```

- [ ] **Step 3: Wrap thermocouple defaults (lines 151–157)**

Find:
```c
    s_config.thermocouple_enabled = true;
    s_config.thermocouple_max_temp = 200.0f;
    s_config.thermocouple_sck_pin = 32;
    s_config.thermocouple_so_pin = 35;
    s_config.thermocouple_cs_pin = 33;
    s_config.thermocouple_min_temp = 0.0f;
    s_config.thermocouple_correction = 0.0f;
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    s_config.thermocouple_enabled = true;
    s_config.thermocouple_max_temp = 200.0f;
    s_config.thermocouple_sck_pin = 32;
    s_config.thermocouple_so_pin = 35;
    s_config.thermocouple_cs_pin = 33;
    s_config.thermocouple_min_temp = 0.0f;
    s_config.thermocouple_correction = 0.0f;
#endif
```

- [ ] **Step 4: Wrap thermocouple alarm defaults (lines 181–183)**

Find:
```c
    s_config.alarm_tc_enabled   = false;
    s_config.alarm_tc_low       = 0.0f;
    s_config.alarm_tc_high      = 0.0f;
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    s_config.alarm_tc_enabled   = false;
    s_config.alarm_tc_low       = 0.0f;
    s_config.alarm_tc_high      = 0.0f;
#endif
```

- [ ] **Step 5: Wrap thermocouple JSON save fields (lines 228–234)**

Find:
```c
    cJSON_AddBoolToObject(sensors, "thermocouple_enabled", s_config.thermocouple_enabled);
    cJSON_AddNumberToObject(sensors, "thermocouple_max_temp", s_config.thermocouple_max_temp);
    cJSON_AddNumberToObject(sensors, "thermocouple_sck_pin", s_config.thermocouple_sck_pin);
    cJSON_AddNumberToObject(sensors, "thermocouple_so_pin", s_config.thermocouple_so_pin);
    cJSON_AddNumberToObject(sensors, "thermocouple_cs_pin", s_config.thermocouple_cs_pin);
    cJSON_AddNumberToObject(sensors, "thermocouple_min_temp", s_config.thermocouple_min_temp);
    cJSON_AddNumberToObject(sensors, "thermocouple_correction", s_config.thermocouple_correction);
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    cJSON_AddBoolToObject(sensors, "thermocouple_enabled", s_config.thermocouple_enabled);
    cJSON_AddNumberToObject(sensors, "thermocouple_max_temp", s_config.thermocouple_max_temp);
    cJSON_AddNumberToObject(sensors, "thermocouple_sck_pin", s_config.thermocouple_sck_pin);
    cJSON_AddNumberToObject(sensors, "thermocouple_so_pin", s_config.thermocouple_so_pin);
    cJSON_AddNumberToObject(sensors, "thermocouple_cs_pin", s_config.thermocouple_cs_pin);
    cJSON_AddNumberToObject(sensors, "thermocouple_min_temp", s_config.thermocouple_min_temp);
    cJSON_AddNumberToObject(sensors, "thermocouple_correction", s_config.thermocouple_correction);
#endif
```

- [ ] **Step 6: Wrap thermocouple alarm JSON save fields (lines 242–244)**

Find:
```c
    cJSON_AddBoolToObject(alarms, "tc_enabled",   s_config.alarm_tc_enabled);
    cJSON_AddNumberToObject(alarms, "tc_low",     s_config.alarm_tc_low);
    cJSON_AddNumberToObject(alarms, "tc_high",    s_config.alarm_tc_high);
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    cJSON_AddBoolToObject(alarms, "tc_enabled",   s_config.alarm_tc_enabled);
    cJSON_AddNumberToObject(alarms, "tc_low",     s_config.alarm_tc_low);
    cJSON_AddNumberToObject(alarms, "tc_high",    s_config.alarm_tc_high);
#endif
```

- [ ] **Step 7: Wrap thermocouple JSON load fields (lines 407–427)**

Find:
```c
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_enabled")) && cJSON_IsBool(item)) {
            s_config.thermocouple_enabled = cJSON_IsTrue(item);
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_max_temp")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_max_temp = (float)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_sck_pin")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_sck_pin = (uint8_t)item->valueint;
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_so_pin")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_so_pin = (uint8_t)item->valueint;
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_cs_pin")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_cs_pin = (uint8_t)item->valueint;
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_min_temp")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_min_temp = (float)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_correction")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_correction = (float)item->valuedouble;
        }
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_enabled")) && cJSON_IsBool(item)) {
            s_config.thermocouple_enabled = cJSON_IsTrue(item);
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_max_temp")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_max_temp = (float)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_sck_pin")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_sck_pin = (uint8_t)item->valueint;
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_so_pin")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_so_pin = (uint8_t)item->valueint;
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_cs_pin")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_cs_pin = (uint8_t)item->valueint;
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_min_temp")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_min_temp = (float)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(sensors, "thermocouple_correction")) && cJSON_IsNumber(item)) {
            s_config.thermocouple_correction = (float)item->valuedouble;
        }
#endif
```

- [ ] **Step 8: Wrap thermocouple alarm JSON load fields (lines 442–447)**

Find:
```c
            if ((item = cJSON_GetObjectItem(alarms, "tc_enabled")) && cJSON_IsBool(item))
                s_config.alarm_tc_enabled = cJSON_IsTrue(item);
            if ((item = cJSON_GetObjectItem(alarms, "tc_low")) && cJSON_IsNumber(item))
                s_config.alarm_tc_low = (float)item->valuedouble;
            if ((item = cJSON_GetObjectItem(alarms, "tc_high")) && cJSON_IsNumber(item))
                s_config.alarm_tc_high = (float)item->valuedouble;
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
            if ((item = cJSON_GetObjectItem(alarms, "tc_enabled")) && cJSON_IsBool(item))
                s_config.alarm_tc_enabled = cJSON_IsTrue(item);
            if ((item = cJSON_GetObjectItem(alarms, "tc_low")) && cJSON_IsNumber(item))
                s_config.alarm_tc_low = (float)item->valuedouble;
            if ((item = cJSON_GetObjectItem(alarms, "tc_high")) && cJSON_IsNumber(item))
                s_config.alarm_tc_high = (float)item->valuedouble;
#endif
```

- [ ] **Step 9: Update thermocouple getter/setter implementations (lines 643–657)**

Find:
```c
bool config_get_thermocouple_enabled(void) { return s_config.thermocouple_enabled; }
float config_get_thermocouple_max_temp(void) { return s_config.thermocouple_max_temp; }
uint8_t config_get_thermocouple_sck_pin(void) { return s_config.thermocouple_sck_pin; }
uint8_t config_get_thermocouple_so_pin(void) { return s_config.thermocouple_so_pin; }
uint8_t config_get_thermocouple_cs_pin(void) { return s_config.thermocouple_cs_pin; }

void config_set_thermocouple_enabled(bool enabled) { s_config.thermocouple_enabled = enabled; }
void config_set_thermocouple_max_temp(float max_temp) { s_config.thermocouple_max_temp = max_temp; }
void config_set_thermocouple_sck_pin(uint8_t pin) { s_config.thermocouple_sck_pin = pin; }
void config_set_thermocouple_so_pin(uint8_t pin) { s_config.thermocouple_so_pin = pin; }
void config_set_thermocouple_cs_pin(uint8_t pin) { s_config.thermocouple_cs_pin = pin; }
float config_get_thermocouple_min_temp(void) { return s_config.thermocouple_min_temp; }
float config_get_thermocouple_correction(void) { return s_config.thermocouple_correction; }
void config_set_thermocouple_min_temp(float min_temp) { s_config.thermocouple_min_temp = min_temp; }
void config_set_thermocouple_correction(float correction) { s_config.thermocouple_correction = correction; }
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
bool config_get_thermocouple_enabled(void) { return s_config.thermocouple_enabled; }
float config_get_thermocouple_max_temp(void) { return s_config.thermocouple_max_temp; }
uint8_t config_get_thermocouple_sck_pin(void) { return s_config.thermocouple_sck_pin; }
uint8_t config_get_thermocouple_so_pin(void) { return s_config.thermocouple_so_pin; }
uint8_t config_get_thermocouple_cs_pin(void) { return s_config.thermocouple_cs_pin; }
void config_set_thermocouple_enabled(bool enabled) { s_config.thermocouple_enabled = enabled; }
void config_set_thermocouple_max_temp(float max_temp) { s_config.thermocouple_max_temp = max_temp; }
void config_set_thermocouple_sck_pin(uint8_t pin) { s_config.thermocouple_sck_pin = pin; }
void config_set_thermocouple_so_pin(uint8_t pin) { s_config.thermocouple_so_pin = pin; }
void config_set_thermocouple_cs_pin(uint8_t pin) { s_config.thermocouple_cs_pin = pin; }
float config_get_thermocouple_min_temp(void) { return s_config.thermocouple_min_temp; }
float config_get_thermocouple_correction(void) { return s_config.thermocouple_correction; }
void config_set_thermocouple_min_temp(float min_temp) { s_config.thermocouple_min_temp = min_temp; }
void config_set_thermocouple_correction(float correction) { s_config.thermocouple_correction = correction; }
#else
bool config_get_thermocouple_enabled(void) { return false; }
float config_get_thermocouple_max_temp(void) { return 0.0f; }
uint8_t config_get_thermocouple_sck_pin(void) { return 0; }
uint8_t config_get_thermocouple_so_pin(void) { return 0; }
uint8_t config_get_thermocouple_cs_pin(void) { return 0; }
void config_set_thermocouple_enabled(bool enabled) { (void)enabled; }
void config_set_thermocouple_max_temp(float max_temp) { (void)max_temp; }
void config_set_thermocouple_sck_pin(uint8_t pin) { (void)pin; }
void config_set_thermocouple_so_pin(uint8_t pin) { (void)pin; }
void config_set_thermocouple_cs_pin(uint8_t pin) { (void)pin; }
float config_get_thermocouple_min_temp(void) { return 0.0f; }
float config_get_thermocouple_correction(void) { return 0.0f; }
void config_set_thermocouple_min_temp(float min_temp) { (void)min_temp; }
void config_set_thermocouple_correction(float correction) { (void)correction; }
#endif
```

- [ ] **Step 10: Update thermocouple alarm getter/setter implementations (lines 716–728)**

Find:
```c
bool  config_get_alarm_tc_enabled(void)   { return s_config.alarm_tc_enabled; }
float config_get_alarm_tc_low(void)       { return s_config.alarm_tc_low; }
float config_get_alarm_tc_high(void)      { return s_config.alarm_tc_high; }

void config_set_alarm_tc_enabled(bool e)   { s_config.alarm_tc_enabled = e; }
void config_set_alarm_tc_low(float v)      { s_config.alarm_tc_low = v; }
void config_set_alarm_tc_high(float v)     { s_config.alarm_tc_high = v; }
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
bool  config_get_alarm_tc_enabled(void)   { return s_config.alarm_tc_enabled; }
float config_get_alarm_tc_low(void)       { return s_config.alarm_tc_low; }
float config_get_alarm_tc_high(void)      { return s_config.alarm_tc_high; }
void config_set_alarm_tc_enabled(bool e)  { s_config.alarm_tc_enabled = e; }
void config_set_alarm_tc_low(float v)     { s_config.alarm_tc_low = v; }
void config_set_alarm_tc_high(float v)    { s_config.alarm_tc_high = v; }
#else
bool  config_get_alarm_tc_enabled(void)   { return false; }
float config_get_alarm_tc_low(void)       { return 0.0f; }
float config_get_alarm_tc_high(void)      { return 0.0f; }
void config_set_alarm_tc_enabled(bool e)  { (void)e; }
void config_set_alarm_tc_low(float v)     { (void)v; }
void config_set_alarm_tc_high(float v)    { (void)v; }
#endif
```

- [ ] **Step 11: Commit**

```bash
git add main/config/config_manager.c
git commit -m "feat(config): guard thermocouple config fields behind CONFIG_THERMOCOUPLE_ENABLED"
```

---

### Task 5: Guard thermocouple fields in api_sensors.c

**Files:**
- Modify: `main/webserver/handlers/api_sensors.c`

Add `thermocouple_hw_enabled` flag to the status response (always present, compile-time constant). Guard config GET/POST thermocouple fields.

- [ ] **Step 1: Add `thermocouple_hw_enabled` and guard status fields**

In `api_sensors_status_handler` (around line 29), find:
```c
    cJSON_AddBoolToObject(root, "thermocouple_valid", data.thermocouple_valid);
    cJSON_AddNumberToObject(root, "thermocouple_temp", data.thermocouple_temp);
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    cJSON_AddBoolToObject(root, "thermocouple_hw_enabled", true);
    cJSON_AddBoolToObject(root, "thermocouple_valid", data.thermocouple_valid);
    cJSON_AddNumberToObject(root, "thermocouple_temp", data.thermocouple_temp);
#else
    cJSON_AddBoolToObject(root, "thermocouple_hw_enabled", false);
#endif
```

- [ ] **Step 2: Guard thermocouple fields in config GET handler**

In `api_sensors_config_get_handler` (around lines 78–94), find:
```c
    cJSON_AddBoolToObject(root, "thermocouple_enabled", config_get_thermocouple_enabled());
    cJSON_AddNumberToObject(root, "thermocouple_max_temp", config_get_thermocouple_max_temp());
    cJSON_AddNumberToObject(root, "thermocouple_sck_pin", config_get_thermocouple_sck_pin());
    cJSON_AddNumberToObject(root, "thermocouple_so_pin", config_get_thermocouple_so_pin());
    cJSON_AddNumberToObject(root, "thermocouple_cs_pin", config_get_thermocouple_cs_pin());
    cJSON_AddNumberToObject(root, "thermocouple_min_temp", config_get_thermocouple_min_temp());
    cJSON_AddNumberToObject(root, "thermocouple_correction", config_get_thermocouple_correction());
```

Replace with:
```c
    cJSON_AddBoolToObject(root, "thermocouple_hw_enabled",
#ifdef CONFIG_THERMOCOUPLE_ENABLED
        true
#else
        false
#endif
    );
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    cJSON_AddBoolToObject(root, "thermocouple_enabled", config_get_thermocouple_enabled());
    cJSON_AddNumberToObject(root, "thermocouple_max_temp", config_get_thermocouple_max_temp());
    cJSON_AddNumberToObject(root, "thermocouple_sck_pin", config_get_thermocouple_sck_pin());
    cJSON_AddNumberToObject(root, "thermocouple_so_pin", config_get_thermocouple_so_pin());
    cJSON_AddNumberToObject(root, "thermocouple_cs_pin", config_get_thermocouple_cs_pin());
    cJSON_AddNumberToObject(root, "thermocouple_min_temp", config_get_thermocouple_min_temp());
    cJSON_AddNumberToObject(root, "thermocouple_correction", config_get_thermocouple_correction());
#endif
```

Also find the alarm tc fields (lines 92–94):
```c
    cJSON_AddBoolToObject(root, "alarm_tc_enabled",   config_get_alarm_tc_enabled());
    cJSON_AddNumberToObject(root, "alarm_tc_low",     config_get_alarm_tc_low());
    cJSON_AddNumberToObject(root, "alarm_tc_high",    config_get_alarm_tc_high());
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    cJSON_AddBoolToObject(root, "alarm_tc_enabled",   config_get_alarm_tc_enabled());
    cJSON_AddNumberToObject(root, "alarm_tc_low",     config_get_alarm_tc_low());
    cJSON_AddNumberToObject(root, "alarm_tc_high",    config_get_alarm_tc_high());
#endif
```

- [ ] **Step 3: Guard thermocouple parsing in config POST handler**

In `api_sensors_config_post_handler`, find the block from line 139:
```c
    if ((item = cJSON_GetObjectItem(json, "thermocouple_enabled")) && cJSON_IsBool(item)) {
        config_set_thermocouple_enabled(cJSON_IsTrue(item));
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_max_temp")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_max_temp((float)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_sck_pin")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_sck_pin((uint8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_so_pin")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_so_pin((uint8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_cs_pin")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_cs_pin((uint8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_min_temp")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_min_temp((float)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_correction")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_correction((float)item->valuedouble);
    }
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    if ((item = cJSON_GetObjectItem(json, "thermocouple_enabled")) && cJSON_IsBool(item)) {
        config_set_thermocouple_enabled(cJSON_IsTrue(item));
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_max_temp")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_max_temp((float)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_sck_pin")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_sck_pin((uint8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_so_pin")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_so_pin((uint8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_cs_pin")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_cs_pin((uint8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_min_temp")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_min_temp((float)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_correction")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_correction((float)item->valuedouble);
    }
#endif
```

Also find the alarm tc POST parsing (lines 178–183):
```c
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_enabled")) && cJSON_IsBool(item))
        config_set_alarm_tc_enabled(cJSON_IsTrue(item));
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_low")) && cJSON_IsNumber(item))
        config_set_alarm_tc_low((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_high")) && cJSON_IsNumber(item))
        config_set_alarm_tc_high((float)item->valuedouble);
```

Replace with:
```c
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_enabled")) && cJSON_IsBool(item))
        config_set_alarm_tc_enabled(cJSON_IsTrue(item));
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_low")) && cJSON_IsNumber(item))
        config_set_alarm_tc_low((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_high")) && cJSON_IsNumber(item))
        config_set_alarm_tc_high((float)item->valuedouble);
#endif
```

- [ ] **Step 4: Commit**

```bash
git add main/webserver/handlers/api_sensors.c
git commit -m "feat(api_sensors): guard thermocouple API fields behind CONFIG_THERMOCOUPLE_ENABLED"
```

---

### Task 6: Verify both variants build cleanly

**Files:** No changes — build verification only.

Build environment: `. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh`

- [ ] **Step 1: Build the standard variant (thermocouple disabled)**

The current `sdkconfig` is from `salt_spray_dev` and likely does NOT have `CONFIG_THERMOCOUPLE_ENABLED`. After adding `Kconfig.projbuild`, ESP-IDF will add it with the default value (`n`) on the next build.

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh
idf.py build 2>&1 | tail -20
```

Expected: `Project build complete.` with no errors. Verify `max6675_driver.c` is NOT in the build output:
```bash
grep -c "max6675" build/compile_commands.json || echo "0 matches (correct)"
```
Expected: `0 matches (correct)`

- [ ] **Step 2: Clean and build the salt_spray variant (thermocouple enabled)**

```bash
idf.py fullclean
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.salt_spray" build 2>&1 | tail -20
```

Expected: `Project build complete.` Verify `max6675_driver.c` IS compiled:
```bash
grep "max6675" build/compile_commands.json | head -2
```
Expected: lines showing `max6675_driver.c` compilation.

- [ ] **Step 3: Commit updated sdkconfig (standard variant)**

After step 1, `sdkconfig` will have been updated by ESP-IDF to include `CONFIG_THERMOCOUPLE_ENABLED=n`. Commit it:

```bash
idf.py fullclean
idf.py build 2>&1 | tail -5
git add sdkconfig
git commit -m "build: update sdkconfig for standard variant (CONFIG_THERMOCOUPLE_ENABLED=n)"
```

---

### Task 7: Replace CI/CD workflows

**Files:**
- Modify: `.github/workflows/release.yml` (rewrite)
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Create `.github/workflows/ci.yml`**

```yaml
name: CI

on:
  pull_request:
    branches:
      - main

jobs:
  validate-source:
    runs-on: ubuntu-latest
    steps:
      - name: Validate source branch
        run: |
          if [ "${{ github.head_ref }}" != "main_dev" ]; then
            echo "::error::PRs to main must come from main_dev (got: ${{ github.head_ref }})"
            exit 1
          fi

  build-standard:
    needs: validate-source
    runs-on: ubuntu-latest
    container:
      image: espressif/idf:v5.5.3
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Build standard variant
        run: |
          . $IDF_PATH/export.sh
          idf.py build

      - name: Upload standard artifacts
        uses: actions/upload-artifact@v4
        with:
          name: firmware-standard-pr${{ github.event.pull_request.number }}
          path: |
            build/lorawan-enddevice.bin
            build/www.bin
            build/bootloader/bootloader.bin
            build/partition_table/partition-table.bin
          retention-days: 7

  build-salt-spray:
    needs: validate-source
    runs-on: ubuntu-latest
    container:
      image: espressif/idf:v5.5.3
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Build salt spray variant
        run: |
          . $IDF_PATH/export.sh
          idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.salt_spray" build

      - name: Upload salt spray artifacts
        uses: actions/upload-artifact@v4
        with:
          name: firmware-salt-spray-pr${{ github.event.pull_request.number }}
          path: |
            build/lorawan-enddevice.bin
            build/bootloader/bootloader.bin
            build/partition_table/partition-table.bin
          retention-days: 7
```

- [ ] **Step 2: Rewrite `.github/workflows/release.yml`**

Replace the entire file content with:

```yaml
name: Release

on:
  push:
    branches:
      - main

jobs:
  build-standard:
    runs-on: ubuntu-latest
    container:
      image: espressif/idf:v5.5.3
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Build standard variant
        run: |
          . $IDF_PATH/export.sh
          idf.py build

      - name: Upload standard artifacts
        uses: actions/upload-artifact@v4
        with:
          name: firmware-standard
          path: |
            build/lorawan-enddevice.bin
            build/www.bin
            build/bootloader/bootloader.bin
            build/partition_table/partition-table.bin
          if-no-files-found: error
          retention-days: 1

  build-salt-spray:
    runs-on: ubuntu-latest
    container:
      image: espressif/idf:v5.5.3
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Build salt spray variant
        run: |
          . $IDF_PATH/export.sh
          idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.salt_spray" build

      - name: Upload salt spray artifacts
        uses: actions/upload-artifact@v4
        with:
          name: firmware-salt-spray
          path: |
            build/lorawan-enddevice.bin
            build/bootloader/bootloader.bin
            build/partition_table/partition-table.bin
          if-no-files-found: error
          retention-days: 1

  release:
    needs: [build-standard, build-salt-spray]
    runs-on: ubuntu-latest
    permissions:
      contents: write
    steps:
      - name: Calculate release number
        id: release_info
        run: |
          COUNT=$(gh api repos/${{ github.repository }}/releases \
            --paginate \
            --jq '.[] | select(.tag_name | test("^r[0-9]+$")) | .tag_name' \
            2>/dev/null | wc -l || echo 0)
          NEXT=$((COUNT + 1))
          echo "tag=r${NEXT}" >> $GITHUB_OUTPUT
          echo "name=Release #${NEXT}" >> $GITHUB_OUTPUT
        env:
          GH_TOKEN: ${{ github.token }}

      - name: Download all artifacts
        uses: actions/download-artifact@v4
        with:
          path: artifacts/

      - name: Rename binaries with tag
        run: |
          TAG="${{ steps.release_info.outputs.tag }}"
          mv artifacts/firmware-standard/lorawan-enddevice.bin   artifacts/lorawan-enddevice-${TAG}-standard.bin
          mv artifacts/firmware-standard/www.bin                 artifacts/www-${TAG}.bin
          mv artifacts/firmware-standard/bootloader/bootloader.bin      artifacts/bootloader-${TAG}.bin
          mv artifacts/firmware-standard/partition_table/partition-table.bin  artifacts/partition-table-${TAG}.bin
          mv artifacts/firmware-salt-spray/lorawan-enddevice.bin artifacts/lorawan-enddevice-${TAG}-salt_spray.bin

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          tag_name: ${{ steps.release_info.outputs.tag }}
          name: ${{ steps.release_info.outputs.name }}
          prerelease: false
          generate_release_notes: true
          files: |
            artifacts/lorawan-enddevice-${{ steps.release_info.outputs.tag }}-standard.bin
            artifacts/lorawan-enddevice-${{ steps.release_info.outputs.tag }}-salt_spray.bin
            artifacts/www-${{ steps.release_info.outputs.tag }}.bin
            artifacts/bootloader-${{ steps.release_info.outputs.tag }}.bin
            artifacts/partition-table-${{ steps.release_info.outputs.tag }}.bin
```

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml .github/workflows/release.yml
git commit -m "ci: split into ci.yml (PR checks) and release.yml (merge to main)"
```

---

### Task 8: Migrate main and main_dev branches

**Note:** `main` has no important work and can be overwritten. This task replaces its history with the current `salt_spray_dev` state.

- [ ] **Step 1: Push current salt_spray_dev to remote**

```bash
git push origin salt_spray_dev
```

- [ ] **Step 2: Reset main_dev to match salt_spray_dev**

```bash
git checkout main_dev
git reset --hard salt_spray_dev
git push origin main_dev --force
git checkout salt_spray_dev
```

- [ ] **Step 3: Reset main to match salt_spray_dev**

`main` is not yet protected, so force-push is allowed:

```bash
git checkout main
git reset --hard salt_spray_dev
git push origin main --force
git checkout salt_spray_dev
```

Expected: `main` on GitHub now matches `salt_spray_dev`.

---

### Task 9: Configure branch protection on main

- [ ] **Step 1: Get repo name**

```bash
REPO=$(gh repo view --json nameWithOwner -q .nameWithOwner)
echo "Repo: $REPO"
```

- [ ] **Step 2: Configure branch protection**

```bash
gh api repos/$REPO/branches/main/protection \
  --method PUT \
  --header "Accept: application/vnd.github+json" \
  --input - <<'EOF'
{
  "required_status_checks": {
    "strict": true,
    "contexts": ["validate-source", "build-standard", "build-salt-spray"]
  },
  "enforce_admins": false,
  "required_pull_request_reviews": null,
  "restrictions": null
}
EOF
```

Expected: HTTP 200 response with the protection rule JSON.

- [ ] **Step 3: Verify protection is active**

```bash
gh api repos/$REPO/branches/main/protection \
  --jq '.required_status_checks.contexts'
```

Expected output:
```json
["validate-source", "build-standard", "build-salt-spray"]
```

---

### Task 10: Archive salt_spray branches

- [ ] **Step 1: Archive branches on GitHub**

```bash
REPO=$(gh repo view --json nameWithOwner -q .nameWithOwner)
# Archive by adding a prefix tag to mark as archived, then optionally delete
# Create archive tags pointing to the branch tips before deleting
git fetch origin
git tag archive/salt_spray origin/salt_spray
git tag archive/salt_spray_dev origin/salt_spray_dev
git push origin archive/salt_spray archive/salt_spray_dev
```

- [ ] **Step 2: Delete the remote branches**

```bash
git push origin --delete salt_spray
git push origin --delete salt_spray_dev
```

- [ ] **Step 3: Delete local branches**

You must be on a different branch before deleting `salt_spray_dev`. Task 8 already switched to `main_dev` as the active branch. From `main_dev`:

```bash
git branch -D salt_spray 2>/dev/null || true
git branch -D salt_spray_dev 2>/dev/null || true
```

Expected: `git branch -a` shows only `main` and `main_dev` (plus `remotes/origin/main` and `remotes/origin/main_dev`).
