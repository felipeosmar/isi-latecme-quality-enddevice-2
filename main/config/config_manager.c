#include "config_manager.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_littlefs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "CONFIG";

// Mutex for thread-safe access
static SemaphoreHandle_t s_config_mutex = NULL;

#define CONFIG_FILE "/userdata/config.json"

// Configuration structure
typedef struct {
    // WiFi
    char wifi_ssid[32];
    char wifi_password[64];
    bool wifi_ap_mode;
    char ap_ssid[32];
    char ap_password[64];

    // LoRaWAN
    char dev_eui[17];         // 16 hex chars + null
    char join_eui[17];        // 16 hex chars + null (AppEUI)
    char app_key[33];         // 32 hex chars + null
    uint8_t lorawan_port;     // FPort for uplinks
    uint32_t uplink_interval; // seconds between uplinks
    uint8_t sub_band;         // AU915 sub-band (1-8)
    bool adr_enabled;         // Adaptive Data Rate

    // Sensors
    uint32_t sensor_interval; // seconds between sensor reads
    float temp_correction;    // temperature correction offset
    float hum_correction;     // humidity correction offset
    char device_name[32];     // device name / hostname
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    bool thermocouple_enabled; // MAX6675 thermocouple
    float thermocouple_max_temp; // max temperature for thermocouple
    uint8_t thermocouple_sck_pin; // SPI clock pin
    uint8_t thermocouple_so_pin;  // SPI data out pin
    uint8_t thermocouple_cs_pin;  // SPI chip select pin
    float thermocouple_min_temp;  // min temperature for thermocouple
    float thermocouple_correction; // temperature correction offset
#endif

    // Interface
    uint8_t buzzer_volume;    // 0-100%

    // LED
    bool led_enabled;
    uint8_t led_brightness;        // 0-100%
    uint32_t led_blink_interval_ms;
    char led_color_normal[8];      // "#RRGGBB" + null
    char led_color_lorawan[8];
    char led_color_wifi[8];
    char led_color_error[8];

    // Web
    char web_username[32];
    char web_password[64];
    bool web_auth_enabled;

    // Alarm thresholds
    bool  alarm_temp_enabled;
    float alarm_temp_low;
    float alarm_temp_high;
    bool  alarm_hum_enabled;
    float alarm_hum_low;
    float alarm_hum_high;
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    bool  alarm_tc_enabled;
    float alarm_tc_low;
    float alarm_tc_high;
#endif

    // Auto-update
    bool  auto_update_enabled;
    char  auto_update_branch[32];
    char  auto_update_firmware_tag[32];
    char  auto_update_www_tag[32];
} config_t;

static config_t s_config;
static bool s_initialized = false;

// ============================================================================
// LittleFS Setup
// ============================================================================

static esp_err_t init_littlefs(void)
{
    ESP_LOGI(TAG, "Initializing LittleFS (userdata partition)");

    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/userdata",
        .partition_label = "userdata",
        .format_if_mount_failed = true,
        .dont_mount = false
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition 'userdata'");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info("userdata", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS userdata: total=%d, used=%d", total, used);
    }

    return ESP_OK;
}

// ============================================================================
// Default Configuration
// ============================================================================

void config_reset_defaults(void)
{
    memset(&s_config, 0, sizeof(s_config));

    // WiFi defaults
    strcpy(s_config.wifi_ssid, "");
    strcpy(s_config.wifi_password, "");
    s_config.wifi_ap_mode = true;
    strcpy(s_config.ap_ssid, "LoRaWAN-Sensor");
    strcpy(s_config.ap_password, "12345678");

    // LoRaWAN defaults
    strcpy(s_config.dev_eui, "");
    strcpy(s_config.join_eui, "");
    strcpy(s_config.app_key, "");
    s_config.lorawan_port = 1;
    s_config.uplink_interval = 60;
    s_config.sub_band = 2;
    s_config.adr_enabled = true;

    // Sensor defaults
    s_config.sensor_interval = 3;
    s_config.temp_correction = 0.0f;
    s_config.hum_correction = 0.0f;
    strcpy(s_config.device_name, "sensor-01");
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    s_config.thermocouple_enabled = true;
    s_config.thermocouple_max_temp = 200.0f;
    s_config.thermocouple_sck_pin = 32;
    s_config.thermocouple_so_pin = 35;
    s_config.thermocouple_cs_pin = 33;
    s_config.thermocouple_min_temp = 0.0f;
    s_config.thermocouple_correction = 0.0f;
#endif

    // Interface defaults
    s_config.buzzer_volume = 30;
    s_config.led_enabled = true;
    s_config.led_brightness = 50;
    s_config.led_blink_interval_ms = 30000;
    strcpy(s_config.led_color_normal, "#FFFFFF");
    strcpy(s_config.led_color_lorawan, "#FFB000");
    strcpy(s_config.led_color_wifi, "#0000FF");
    strcpy(s_config.led_color_error, "#FF0000");

    // Web defaults
    strcpy(s_config.web_username, "admin");
    strcpy(s_config.web_password, "admin");
    s_config.web_auth_enabled = true;

    // Alarm thresholds (all disabled by default)
    s_config.alarm_temp_enabled = false;
    s_config.alarm_temp_low     = 0.0f;
    s_config.alarm_temp_high    = 0.0f;
    s_config.alarm_hum_enabled  = false;
    s_config.alarm_hum_low      = 0.0f;
    s_config.alarm_hum_high     = 0.0f;
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    s_config.alarm_tc_enabled   = false;
    s_config.alarm_tc_low       = 0.0f;
    s_config.alarm_tc_high      = 0.0f;
#endif

    // Auto-update defaults
    s_config.auto_update_enabled = true;
    strcpy(s_config.auto_update_branch, "main");
    s_config.auto_update_firmware_tag[0] = '\0';
    s_config.auto_update_www_tag[0] = '\0';

    ESP_LOGI(TAG, "Configuration reset to defaults");
}

// ============================================================================
// Load/Save
// ============================================================================

static esp_err_t _config_save_internal(void)
{
    cJSON *root = cJSON_CreateObject();

    // WiFi section
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "ssid", s_config.wifi_ssid);
    cJSON_AddStringToObject(wifi, "password", s_config.wifi_password);
    cJSON_AddBoolToObject(wifi, "ap_mode", s_config.wifi_ap_mode);
    cJSON_AddStringToObject(wifi, "ap_ssid", s_config.ap_ssid);
    cJSON_AddStringToObject(wifi, "ap_password", s_config.ap_password);
    cJSON_AddItemToObject(root, "wifi", wifi);

    // LoRaWAN section
    cJSON *lorawan = cJSON_CreateObject();
    cJSON_AddStringToObject(lorawan, "dev_eui", s_config.dev_eui);
    cJSON_AddStringToObject(lorawan, "join_eui", s_config.join_eui);
    cJSON_AddStringToObject(lorawan, "app_key", s_config.app_key);
    cJSON_AddNumberToObject(lorawan, "port", s_config.lorawan_port);
    cJSON_AddNumberToObject(lorawan, "uplink_interval", s_config.uplink_interval);
    cJSON_AddNumberToObject(lorawan, "sub_band", s_config.sub_band);
    cJSON_AddBoolToObject(lorawan, "adr_enabled", s_config.adr_enabled);
    cJSON_AddItemToObject(root, "lorawan", lorawan);

    // Sensors section
    cJSON *sensors = cJSON_CreateObject();
    cJSON_AddNumberToObject(sensors, "interval", s_config.sensor_interval);
    cJSON_AddNumberToObject(sensors, "temp_correction", s_config.temp_correction);
    cJSON_AddNumberToObject(sensors, "hum_correction", s_config.hum_correction);
    cJSON_AddStringToObject(sensors, "device_name", s_config.device_name);
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    cJSON_AddBoolToObject(sensors, "thermocouple_enabled", s_config.thermocouple_enabled);
    cJSON_AddNumberToObject(sensors, "thermocouple_max_temp", s_config.thermocouple_max_temp);
    cJSON_AddNumberToObject(sensors, "thermocouple_sck_pin", s_config.thermocouple_sck_pin);
    cJSON_AddNumberToObject(sensors, "thermocouple_so_pin", s_config.thermocouple_so_pin);
    cJSON_AddNumberToObject(sensors, "thermocouple_cs_pin", s_config.thermocouple_cs_pin);
    cJSON_AddNumberToObject(sensors, "thermocouple_min_temp", s_config.thermocouple_min_temp);
    cJSON_AddNumberToObject(sensors, "thermocouple_correction", s_config.thermocouple_correction);
#endif
    cJSON *alarms = cJSON_CreateObject();
    cJSON_AddBoolToObject(alarms, "temp_enabled", s_config.alarm_temp_enabled);
    cJSON_AddNumberToObject(alarms, "temp_low",   s_config.alarm_temp_low);
    cJSON_AddNumberToObject(alarms, "temp_high",  s_config.alarm_temp_high);
    cJSON_AddBoolToObject(alarms, "hum_enabled",  s_config.alarm_hum_enabled);
    cJSON_AddNumberToObject(alarms, "hum_low",    s_config.alarm_hum_low);
    cJSON_AddNumberToObject(alarms, "hum_high",   s_config.alarm_hum_high);
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    cJSON_AddBoolToObject(alarms, "tc_enabled",   s_config.alarm_tc_enabled);
    cJSON_AddNumberToObject(alarms, "tc_low",     s_config.alarm_tc_low);
    cJSON_AddNumberToObject(alarms, "tc_high",    s_config.alarm_tc_high);
#endif
    cJSON_AddItemToObject(sensors, "alarms", alarms);
    cJSON_AddItemToObject(root, "sensors", sensors);

    // Interface section
    cJSON *interface = cJSON_CreateObject();
    cJSON_AddNumberToObject(interface, "buzzer_volume", s_config.buzzer_volume);
    cJSON *led = cJSON_CreateObject();
    cJSON_AddBoolToObject(led, "enabled", s_config.led_enabled);
    cJSON_AddNumberToObject(led, "brightness", s_config.led_brightness);
    cJSON_AddNumberToObject(led, "blink_interval_ms", s_config.led_blink_interval_ms);
    cJSON_AddStringToObject(led, "color_normal", s_config.led_color_normal);
    cJSON_AddStringToObject(led, "color_lorawan", s_config.led_color_lorawan);
    cJSON_AddStringToObject(led, "color_wifi", s_config.led_color_wifi);
    cJSON_AddStringToObject(led, "color_error", s_config.led_color_error);
    cJSON_AddItemToObject(interface, "led", led);
    cJSON_AddItemToObject(root, "interface", interface);

    // Web section
    cJSON *web = cJSON_CreateObject();
    cJSON_AddStringToObject(web, "username", s_config.web_username);
    cJSON_AddStringToObject(web, "password", s_config.web_password);
    cJSON_AddBoolToObject(web, "auth_enabled", s_config.web_auth_enabled);
    cJSON_AddItemToObject(root, "web", web);

    // Auto-update section
    cJSON *update = cJSON_CreateObject();
    cJSON_AddBoolToObject(update, "enabled", s_config.auto_update_enabled);
    cJSON_AddStringToObject(update, "branch", s_config.auto_update_branch);
    cJSON_AddStringToObject(update, "firmware_tag", s_config.auto_update_firmware_tag);
    cJSON_AddStringToObject(update, "www_tag", s_config.auto_update_www_tag);
    cJSON_AddItemToObject(root, "auto_update", update);

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *f = fopen(CONFIG_FILE, "w");
    if (f == NULL) {
        free(json_str);
        ESP_LOGE(TAG, "Failed to open config file for writing");
        return ESP_FAIL;
    }

    fprintf(f, "%s", json_str);
    fclose(f);
    free(json_str);

    ESP_LOGI(TAG, "Configuration saved");
    return ESP_OK;
}

esp_err_t config_load(void)
{
    if (s_config_mutex && xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire config mutex for load");
        return ESP_ERR_TIMEOUT;
    }

    FILE *f = fopen(CONFIG_FILE, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "Config file not found, using defaults");
        config_reset_defaults();
        esp_err_t ret = _config_save_internal();
        if (s_config_mutex) xSemaphoreGive(s_config_mutex);
        return ret;
    }

    // Read file
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_str = malloc(fsize + 1);
    if (json_str == NULL) {
        fclose(f);
        if (s_config_mutex) xSemaphoreGive(s_config_mutex);
        return ESP_ERR_NO_MEM;
    }

    fread(json_str, 1, fsize, f);
    json_str[fsize] = '\0';
    fclose(f);

    // Parse JSON
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (root == NULL) {
        ESP_LOGW(TAG, "Config file corrupt, resetting to defaults");
        config_reset_defaults();
        _config_save_internal();
        if (s_config_mutex) xSemaphoreGive(s_config_mutex);
        return ESP_OK;
    }

    // WiFi section
    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    if (wifi) {
        cJSON *item;
        if ((item = cJSON_GetObjectItem(wifi, "ssid")) && cJSON_IsString(item)) {
            strncpy(s_config.wifi_ssid, item->valuestring, sizeof(s_config.wifi_ssid) - 1);
        }
        if ((item = cJSON_GetObjectItem(wifi, "password")) && cJSON_IsString(item)) {
            strncpy(s_config.wifi_password, item->valuestring, sizeof(s_config.wifi_password) - 1);
        }
        if ((item = cJSON_GetObjectItem(wifi, "ap_mode")) && cJSON_IsBool(item)) {
            s_config.wifi_ap_mode = cJSON_IsTrue(item);
        }
        if ((item = cJSON_GetObjectItem(wifi, "ap_ssid")) && cJSON_IsString(item)) {
            strncpy(s_config.ap_ssid, item->valuestring, sizeof(s_config.ap_ssid) - 1);
        }
        if ((item = cJSON_GetObjectItem(wifi, "ap_password")) && cJSON_IsString(item)) {
            strncpy(s_config.ap_password, item->valuestring, sizeof(s_config.ap_password) - 1);
        }
    }

    // LoRaWAN section
    cJSON *lorawan = cJSON_GetObjectItem(root, "lorawan");
    if (lorawan) {
        cJSON *item;
        if ((item = cJSON_GetObjectItem(lorawan, "dev_eui")) && cJSON_IsString(item)) {
            strncpy(s_config.dev_eui, item->valuestring, sizeof(s_config.dev_eui) - 1);
        }
        if ((item = cJSON_GetObjectItem(lorawan, "join_eui")) && cJSON_IsString(item)) {
            strncpy(s_config.join_eui, item->valuestring, sizeof(s_config.join_eui) - 1);
        }
        if ((item = cJSON_GetObjectItem(lorawan, "app_key")) && cJSON_IsString(item)) {
            strncpy(s_config.app_key, item->valuestring, sizeof(s_config.app_key) - 1);
        }
        if ((item = cJSON_GetObjectItem(lorawan, "port")) && cJSON_IsNumber(item)) {
            s_config.lorawan_port = (uint8_t)item->valueint;
        }
        if ((item = cJSON_GetObjectItem(lorawan, "uplink_interval")) && cJSON_IsNumber(item)) {
            s_config.uplink_interval = (uint32_t)item->valueint;
        }
        if ((item = cJSON_GetObjectItem(lorawan, "sub_band")) && cJSON_IsNumber(item)) {
            s_config.sub_band = (uint8_t)item->valueint;
        }
        if ((item = cJSON_GetObjectItem(lorawan, "adr_enabled")) && cJSON_IsBool(item)) {
            s_config.adr_enabled = cJSON_IsTrue(item);
        }
    }

    // Sensors section
    cJSON *sensors = cJSON_GetObjectItem(root, "sensors");
    if (sensors) {
        cJSON *item;
        if ((item = cJSON_GetObjectItem(sensors, "interval")) && cJSON_IsNumber(item)) {
            s_config.sensor_interval = (uint32_t)item->valueint;
        }
        if ((item = cJSON_GetObjectItem(sensors, "temp_correction")) && cJSON_IsNumber(item)) {
            s_config.temp_correction = (float)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(sensors, "hum_correction")) && cJSON_IsNumber(item)) {
            s_config.hum_correction = (float)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(sensors, "device_name")) && cJSON_IsString(item)) {
            strncpy(s_config.device_name, item->valuestring, sizeof(s_config.device_name) - 1);
        }
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
#ifdef CONFIG_THERMOCOUPLE_ENABLED
            if ((item = cJSON_GetObjectItem(alarms, "tc_enabled")) && cJSON_IsBool(item))
                s_config.alarm_tc_enabled = cJSON_IsTrue(item);
            if ((item = cJSON_GetObjectItem(alarms, "tc_low")) && cJSON_IsNumber(item))
                s_config.alarm_tc_low = (float)item->valuedouble;
            if ((item = cJSON_GetObjectItem(alarms, "tc_high")) && cJSON_IsNumber(item))
                s_config.alarm_tc_high = (float)item->valuedouble;
#endif
        }
    }

    // Interface section
    cJSON *interface = cJSON_GetObjectItem(root, "interface");
    if (interface) {
        cJSON *item;
        if ((item = cJSON_GetObjectItem(interface, "buzzer_volume")) && cJSON_IsNumber(item)) {
            uint8_t vol = (uint8_t)item->valueint;
            s_config.buzzer_volume = (vol > 100) ? 100 : vol;
        }
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
    }

    // Web section
    cJSON *web = cJSON_GetObjectItem(root, "web");
    if (web) {
        cJSON *item;
        if ((item = cJSON_GetObjectItem(web, "username")) && cJSON_IsString(item)) {
            strncpy(s_config.web_username, item->valuestring, sizeof(s_config.web_username) - 1);
        }
        if ((item = cJSON_GetObjectItem(web, "password")) && cJSON_IsString(item)) {
            strncpy(s_config.web_password, item->valuestring, sizeof(s_config.web_password) - 1);
        }
        if ((item = cJSON_GetObjectItem(web, "auth_enabled")) && cJSON_IsBool(item)) {
            s_config.web_auth_enabled = cJSON_IsTrue(item);
        }
    }

    // Auto-update section
    cJSON *au = cJSON_GetObjectItem(root, "auto_update");
    if (au) {
        cJSON *item;
        if ((item = cJSON_GetObjectItem(au, "enabled")) && cJSON_IsBool(item)) {
            s_config.auto_update_enabled = cJSON_IsTrue(item);
        }
        if ((item = cJSON_GetObjectItem(au, "branch")) && cJSON_IsString(item)) {
            strncpy(s_config.auto_update_branch, item->valuestring,
                    sizeof(s_config.auto_update_branch) - 1);
        }
        if ((item = cJSON_GetObjectItem(au, "firmware_tag")) && cJSON_IsString(item)) {
            strncpy(s_config.auto_update_firmware_tag, item->valuestring,
                    sizeof(s_config.auto_update_firmware_tag) - 1);
        }
        if ((item = cJSON_GetObjectItem(au, "www_tag")) && cJSON_IsString(item)) {
            strncpy(s_config.auto_update_www_tag, item->valuestring,
                    sizeof(s_config.auto_update_www_tag) - 1);
        }
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Configuration loaded");
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
    return ESP_OK;
}

esp_err_t config_save(void)
{
    if (s_config_mutex && xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire config mutex for save");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = _config_save_internal();

    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
    return ret;
}

esp_err_t config_factory_reset(void)
{
    if (s_config_mutex && xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire config mutex for factory reset");
        return ESP_ERR_TIMEOUT;
    }

    config_reset_defaults();
    esp_err_t ret = _config_save_internal();

    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
    return ret;
}

// ============================================================================
// Init/Deinit
// ============================================================================

esp_err_t config_init(void)
{
    if (s_initialized) return ESP_OK;

    // Create mutex for thread safety
    if (s_config_mutex == NULL) {
        s_config_mutex = xSemaphoreCreateMutex();
        if (s_config_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create config mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    config_reset_defaults();

    esp_err_t ret = init_littlefs();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = config_load();
    s_initialized = true;
    return ret;
}

void config_deinit(void)
{
    esp_vfs_littlefs_unregister("userdata");
    s_initialized = false;
}

// ============================================================================
// Getters/Setters - WiFi
// ============================================================================

const char* config_get_wifi_ssid(void) { return s_config.wifi_ssid; }
const char* config_get_wifi_password(void) { return s_config.wifi_password; }
bool config_get_wifi_ap_mode(void) { return s_config.wifi_ap_mode; }
const char* config_get_ap_ssid(void) { return s_config.ap_ssid; }
const char* config_get_ap_password(void) { return s_config.ap_password; }

void config_set_wifi_ssid(const char *ssid) {
    if (ssid) strncpy(s_config.wifi_ssid, ssid, sizeof(s_config.wifi_ssid) - 1);
}
void config_set_wifi_password(const char *password) {
    if (password) strncpy(s_config.wifi_password, password, sizeof(s_config.wifi_password) - 1);
}
void config_set_wifi_ap_mode(bool ap_mode) { s_config.wifi_ap_mode = ap_mode; }
void config_set_ap_ssid(const char *ssid) {
    if (ssid) strncpy(s_config.ap_ssid, ssid, sizeof(s_config.ap_ssid) - 1);
}
void config_set_ap_password(const char *password) {
    if (password) strncpy(s_config.ap_password, password, sizeof(s_config.ap_password) - 1);
}

// ============================================================================
// Getters/Setters - LoRaWAN
// ============================================================================

const char* config_get_dev_eui(void) { return s_config.dev_eui; }
const char* config_get_join_eui(void) { return s_config.join_eui; }
const char* config_get_app_key(void) { return s_config.app_key; }
uint8_t config_get_lorawan_port(void) { return s_config.lorawan_port; }
uint32_t config_get_uplink_interval(void) { return s_config.uplink_interval; }
uint8_t config_get_sub_band(void) { return s_config.sub_band; }
bool config_get_adr_enabled(void) { return s_config.adr_enabled; }

void config_set_dev_eui(const char *eui) {
    if (eui) strncpy(s_config.dev_eui, eui, sizeof(s_config.dev_eui) - 1);
}
void config_set_join_eui(const char *eui) {
    if (eui) strncpy(s_config.join_eui, eui, sizeof(s_config.join_eui) - 1);
}
void config_set_app_key(const char *key) {
    if (key) strncpy(s_config.app_key, key, sizeof(s_config.app_key) - 1);
}
void config_set_lorawan_port(uint8_t port) { s_config.lorawan_port = port; }
void config_set_uplink_interval(uint32_t seconds) { s_config.uplink_interval = seconds; }
void config_set_sub_band(uint8_t band) { s_config.sub_band = band; }
void config_set_adr_enabled(bool enabled) { s_config.adr_enabled = enabled; }

// ============================================================================
// Getters/Setters - Sensors
// ============================================================================

uint32_t config_get_sensor_interval(void) { return s_config.sensor_interval; }
float config_get_temp_correction(void) { return s_config.temp_correction; }
float config_get_hum_correction(void) { return s_config.hum_correction; }
const char* config_get_device_name(void) { return s_config.device_name; }

void config_set_sensor_interval(uint32_t seconds) { s_config.sensor_interval = seconds; }
void config_set_temp_correction(float correction) { s_config.temp_correction = correction; }
void config_set_hum_correction(float correction) { s_config.hum_correction = correction; }
void config_set_device_name(const char *name) {
    if (name) strncpy(s_config.device_name, name, sizeof(s_config.device_name) - 1);
}

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

// ============================================================================
// Getters/Setters - Interface
// ============================================================================

uint8_t config_get_buzzer_volume(void) { return s_config.buzzer_volume; }
void config_set_buzzer_volume(uint8_t volume) { s_config.buzzer_volume = (volume > 100) ? 100 : volume; }

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

// ============================================================================
// Getters/Setters - Web
// ============================================================================

const char* config_get_web_username(void) { return s_config.web_username; }
const char* config_get_web_password(void) { return s_config.web_password; }
bool config_get_web_auth_enabled(void) { return s_config.web_auth_enabled; }

void config_set_web_username(const char *username) {
    if (username) strncpy(s_config.web_username, username, sizeof(s_config.web_username) - 1);
}
void config_set_web_password(const char *password) {
    if (password) strncpy(s_config.web_password, password, sizeof(s_config.web_password) - 1);
}
void config_set_web_auth_enabled(bool enabled) { s_config.web_auth_enabled = enabled; }

// ============================================================================
// Getters/Setters - Alarm Thresholds
// ============================================================================

bool  config_get_alarm_temp_enabled(void) { return s_config.alarm_temp_enabled; }
float config_get_alarm_temp_low(void)     { return s_config.alarm_temp_low; }
float config_get_alarm_temp_high(void)    { return s_config.alarm_temp_high; }
bool  config_get_alarm_hum_enabled(void)  { return s_config.alarm_hum_enabled; }
float config_get_alarm_hum_low(void)      { return s_config.alarm_hum_low; }
float config_get_alarm_hum_high(void)     { return s_config.alarm_hum_high; }
#ifdef CONFIG_THERMOCOUPLE_ENABLED
bool  config_get_alarm_tc_enabled(void)   { return s_config.alarm_tc_enabled; }
float config_get_alarm_tc_low(void)       { return s_config.alarm_tc_low; }
float config_get_alarm_tc_high(void)      { return s_config.alarm_tc_high; }
#else
bool  config_get_alarm_tc_enabled(void)   { return false; }
float config_get_alarm_tc_low(void)       { return 0.0f; }
float config_get_alarm_tc_high(void)      { return 0.0f; }
#endif

void config_set_alarm_temp_enabled(bool e) { s_config.alarm_temp_enabled = e; }
void config_set_alarm_temp_low(float v)    { s_config.alarm_temp_low = v; }
void config_set_alarm_temp_high(float v)   { s_config.alarm_temp_high = v; }
void config_set_alarm_hum_enabled(bool e)  { s_config.alarm_hum_enabled = e; }
void config_set_alarm_hum_low(float v)     { s_config.alarm_hum_low = v; }
void config_set_alarm_hum_high(float v)    { s_config.alarm_hum_high = v; }
#ifdef CONFIG_THERMOCOUPLE_ENABLED
void config_set_alarm_tc_enabled(bool e)  { s_config.alarm_tc_enabled = e; }
void config_set_alarm_tc_low(float v)     { s_config.alarm_tc_low = v; }
void config_set_alarm_tc_high(float v)    { s_config.alarm_tc_high = v; }
#else
void config_set_alarm_tc_enabled(bool e)  { (void)e; }
void config_set_alarm_tc_low(float v)     { (void)v; }
void config_set_alarm_tc_high(float v)    { (void)v; }
#endif

// ============================================================================
// Auto-Update Configuration
// ============================================================================

bool config_get_auto_update_enabled(void) {
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000));
    bool v = s_config.auto_update_enabled;
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
    return v;
}

// String getters return a direct pointer to internal buffer (no mutex).
// Callers must copy the value immediately (e.g. into strncpy/snprintf).
const char *config_get_auto_update_branch(void) {
    return s_config.auto_update_branch;
}

const char *config_get_auto_update_firmware_tag(void) {
    return s_config.auto_update_firmware_tag;
}

const char *config_get_auto_update_www_tag(void) {
    return s_config.auto_update_www_tag;
}

void config_set_auto_update_enabled(bool enabled) {
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000));
    s_config.auto_update_enabled = enabled;
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
}

void config_set_auto_update_branch(const char *branch) {
    if (!branch) return;
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000));
    strncpy(s_config.auto_update_branch, branch, sizeof(s_config.auto_update_branch) - 1);
    s_config.auto_update_branch[sizeof(s_config.auto_update_branch) - 1] = '\0';
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
}

void config_set_auto_update_firmware_tag(const char *tag) {
    if (!tag) return;
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000));
    strncpy(s_config.auto_update_firmware_tag, tag, sizeof(s_config.auto_update_firmware_tag) - 1);
    s_config.auto_update_firmware_tag[sizeof(s_config.auto_update_firmware_tag) - 1] = '\0';
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
}

void config_set_auto_update_www_tag(const char *tag) {
    if (!tag) return;
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(1000));
    strncpy(s_config.auto_update_www_tag, tag, sizeof(s_config.auto_update_www_tag) - 1);
    s_config.auto_update_www_tag[sizeof(s_config.auto_update_www_tag) - 1] = '\0';
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
}
