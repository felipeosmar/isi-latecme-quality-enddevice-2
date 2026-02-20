/**
 * @file main.c
 * @brief ESP32 LoRaWAN End Device - Sensor Data Collection for ChirpStack
 *
 * This application provides:
 * - LoRaWAN communication via OTAA to ChirpStack
 * - Sensor data collection (temperature, humidity, thermocouple)
 * - CayenneLPP payload encoding
 * - OLED display for status
 * - Web interface for configuration and monitoring
 * - WiFi AP/STA mode
 * - JSON-based configuration storage
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

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

static const char *TAG = "MAIN";

/**
 * @brief Initialize WiFi based on configuration
 */
static esp_err_t init_wifi(void)
{
    esp_err_t ret;

    // Initialize WiFi manager
    ret = wifi_manager_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi manager: %s", esp_err_to_name(ret));
        return ret;
    }

    // Check if we have saved WiFi credentials
    const char *ssid = config_get_wifi_ssid();
    const char *password = config_get_wifi_password();
    bool ap_mode = config_get_wifi_ap_mode();

    if (!ap_mode && strlen(ssid) > 0) {
        // Try to connect to saved network
        ESP_LOGI(TAG, "Connecting to saved network: %s", ssid);
        ret = wifi_manager_connect(ssid, password);

        if (ret == ESP_OK) {
            // 2 rising tones on WiFi connection
            buzzer_tone(2200, 80);
            vTaskDelay(pdMS_TO_TICKS(60));
            buzzer_tone(3000, 100);
        } else {
            ESP_LOGW(TAG, "Failed to connect, starting AP mode");
            ap_mode = true;
        }
    } else {
        ap_mode = true;
    }

    if (ap_mode) {
        // Start AP mode for configuration
        const char *ap_ssid = config_get_ap_ssid();
        const char *ap_pass = config_get_ap_password();
        ESP_LOGI(TAG, "Starting AP mode: %s", ap_ssid);
        ret = wifi_manager_start_ap(ap_ssid, ap_pass);
    }

    return ret;
}

/**
 * @brief Display update task - cycles OLED display pages
 */
static void display_task(void *param)
{
    ESP_LOGI(TAG, "Display task started");

    // Wait for sensor manager to initialize I2C bus
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

    uint32_t page_timer = 0;
    const uint32_t page_cycle_ms = 5000; // Cycle pages every 5 seconds

    while (1) {
        oled_page_t page = oled_display_get_page();

        switch (page) {
            case OLED_PAGE_SENSORS: {
                sensor_data_t data;
                sensor_manager_get_data(&data);
                float tc = data.thermocouple_valid ? data.thermocouple_temp : NAN;
                float t = data.temp_hum_valid ? data.temperature : NAN;
                float h = data.temp_hum_valid ? data.humidity : NAN;
                oled_display_show_sensors(t, h, tc);
                break;
            }
            case OLED_PAGE_SYSTEM: {
                char ip[16] = "N/A";
                wifi_manager_get_ip(ip);
                uint32_t uptime_s = xTaskGetTickCount() / configTICK_RATE_HZ;
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

        vTaskDelay(pdMS_TO_TICKS(1000));
        page_timer += 1000;

        if (page_timer >= page_cycle_ms) {
            oled_display_next_page();
            page_timer = 0;
        }
    }
}

/**
 * @brief Uplink task - builds CayenneLPP payload and sends via LoRaWAN
 */
static void uplink_task(void *param)
{
    ESP_LOGI(TAG, "Uplink task started");

    // Wait for LoRaWAN to join
    while (!lorawan_is_joined()) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    ESP_LOGI(TAG, "LoRaWAN joined, starting uplinks");

    while (1) {
        uint32_t interval_s = config_get_uplink_interval();
        if (interval_s < 10) interval_s = 10;

        vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));

        if (!lorawan_is_joined()) {
            continue;
        }

        // Build CayenneLPP payload from sensor data
        sensor_data_t data;
        sensor_manager_get_data(&data);

        cayenne_lpp_t lpp;
        cayenne_lpp_reset(&lpp);

        if (data.temp_hum_valid) {
            cayenne_lpp_add_temperature(&lpp, 1, data.temperature);
            cayenne_lpp_add_humidity(&lpp, 2, data.humidity);
        }

        if (data.thermocouple_valid) {
            cayenne_lpp_add_temperature(&lpp, 4, data.thermocouple_temp);
        }

        size_t payload_size = cayenne_lpp_get_size(&lpp);
        if (payload_size > 0) {
            uint8_t port = config_get_lorawan_port();
            lorawan_send(cayenne_lpp_get_buffer(&lpp), payload_size, port, false);
        }
    }
}

void app_main(void)
{
    // Initialize log buffer FIRST to capture all logs
    log_buffer_init();

    // Set log level to WARN (suppress INFO and DEBUG messages)
    esp_log_level_set("*", ESP_LOG_WARN);

    // Allow important startup messages
    esp_log_level_set("MAIN", ESP_LOG_INFO);
    esp_log_level_set("LORAWAN", ESP_LOG_INFO);
    esp_log_level_set("SENSOR_MGR", ESP_LOG_INFO);
    esp_log_level_set("MAX6675", ESP_LOG_DEBUG);
    esp_log_level_set("OLED", ESP_LOG_INFO);

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "  LoRaWAN End Device - Sensor Node");
    ESP_LOGI(TAG, "==========================================");

    // Initialize buzzer and play startup melody (3 rising tones)
    buzzer_init();
    buzzer_tone(1800, 80);
    vTaskDelay(pdMS_TO_TICKS(60));
    buzzer_tone(2400, 80);
    vTaskDelay(pdMS_TO_TICKS(60));
    buzzer_tone(3200, 100);

    // Initialize configuration manager (includes LittleFS)
    ESP_LOGI(TAG, "Initializing configuration...");
    if (config_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize configuration!");
        return;
    }

    // Apply buzzer volume from config
    buzzer_set_volume(config_get_buzzer_volume());

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

    // Initialize health monitor (starts monitoring task)
    ESP_LOGI(TAG, "Starting health monitor...");
    if (health_monitor_init() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start health monitor");
    }

    // Spawn FreeRTOS tasks
    ESP_LOGI(TAG, "Starting FreeRTOS tasks...");

    // Sensor task - Core 0, Priority 5, Stack 4096
    xTaskCreatePinnedToCore(sensor_task, "sensor", 6144, NULL, 5, NULL, 0);

    // LoRaWAN task - Core 1, Priority 6, Stack 8192
    xTaskCreatePinnedToCore(lorawan_task, "lorawan", 8192, NULL, 6, NULL, 1);

    // Uplink task - Core 0, Priority 4, Stack 4096
    xTaskCreatePinnedToCore(uplink_task, "uplink", 4096, NULL, 4, NULL, 0);

    // Display task - Core 0, Priority 3, Stack 4096
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 3, NULL, 0);

    ESP_LOGI(TAG, "System ready!");
}
