/**
 * @file lorawan_handler.cpp
 * @brief LoRaWAN handler implementation using RadioLib
 *
 * Implements LoRaWAN Class A with OTAA activation using RadioLib.
 * Uses SX1276/SX1278 radio via SPI on ESP32.
 * Configured for AU915 frequency plan with sub-band selection.
 */

#include "lorawan_handler.h"
#include "EspHal.h"
#include "config_manager.h"

#include <RadioLib.h>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "LORAWAN";

// ============================================================================
// Hardware Pin Definitions
// ============================================================================

// SPI pins for SX127x
#define RADIO_SCLK      18
#define RADIO_MISO       19
#define RADIO_MOSI       23

// Radio control pins
#define RADIO_NSS        5
#define RADIO_RST        14
#define RADIO_DIO0       26
#define RADIO_DIO1       13

// ============================================================================
// RadioLib Objects
// ============================================================================

static EspHal *hal = nullptr;
static SX1276 *radio = nullptr;
static LoRaWANNode *node = nullptr;
static SemaphoreHandle_t lorawan_mutex = nullptr;

// ============================================================================
// State
// ============================================================================

static lorawan_stats_t stats = {};
static bool initialized = false;

// TODO: NVM buffer for LoRaWAN session persistence (future enhancement)

// ============================================================================
// Helper: Parse hex string to byte array
// ============================================================================

static bool hex_to_bytes(const char *hex, uint8_t *bytes, size_t byte_len)
{
    size_t hex_len = strlen(hex);
    if (hex_len != byte_len * 2) {
        return false;
    }

    for (size_t i = 0; i < byte_len; i++) {
        char byte_str[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        char *endptr;
        unsigned long val = strtoul(byte_str, &endptr, 16);
        if (*endptr != '\0') {
            return false;
        }
        bytes[i] = (uint8_t)val;
    }
    return true;
}

// ============================================================================
// Helper: Parse hex string to uint64_t (for DevEUI/JoinEUI)
// ============================================================================

static bool hex_to_u64(const char *hex, uint64_t *out)
{
    if (strlen(hex) != 16) {
        return false;
    }

    char *endptr;
    *out = strtoull(hex, &endptr, 16);
    return (*endptr == '\0');
}

// ============================================================================
// Public API
// ============================================================================

extern "C" esp_err_t lorawan_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    lorawan_mutex = xSemaphoreCreateMutex();
    if (!lorawan_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create HAL with SPI pins
    hal = new EspHal(RADIO_SCLK, RADIO_MISO, RADIO_MOSI);

    // Create radio module: Module(hal, NSS, DIO0, RST, DIO1)
    Module *mod = new Module(hal, RADIO_NSS, RADIO_DIO0, RADIO_RST, RADIO_DIO1);
    radio = new SX1276(mod);

    // Initialize the radio
    ESP_LOGI(TAG, "Initializing SX1276 radio...");
    int state = radio->begin();
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "Radio init failed, code %d", state);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SX1276 initialized successfully");

    // Read sub-band from config
    uint8_t sub_band = config_get_sub_band();

    // Create LoRaWAN node: LoRaWANNode(radio, region, sub_band)
    node = new LoRaWANNode(radio, &AU915, sub_band);

    // Set ADR from config
    bool adr = config_get_adr_enabled();
    if (adr) {
        node->setADR(true);
    }

    memset(&stats, 0, sizeof(stats));
    initialized = true;

    ESP_LOGI(TAG, "LoRaWAN handler initialized (AU915 sub-band %d, ADR %s)",
             sub_band, adr ? "on" : "off");

    return ESP_OK;
}

extern "C" esp_err_t lorawan_join(void)
{
    if (!initialized || !node) {
        return ESP_ERR_INVALID_STATE;
    }

    // Read credentials from config
    const char *dev_eui_str = config_get_dev_eui();
    const char *join_eui_str = config_get_join_eui();
    const char *app_key_str = config_get_app_key();

    if (strlen(dev_eui_str) != 16 || strlen(join_eui_str) != 16 || strlen(app_key_str) != 32) {
        ESP_LOGE(TAG, "Invalid credentials (DevEUI=%zu, JoinEUI=%zu, AppKey=%zu chars)",
                 strlen(dev_eui_str), strlen(join_eui_str), strlen(app_key_str));
        return ESP_ERR_INVALID_ARG;
    }

    // Parse credentials
    uint64_t dev_eui, join_eui;
    uint8_t app_key[16];
    uint8_t nwk_key[16];

    if (!hex_to_u64(dev_eui_str, &dev_eui)) {
        ESP_LOGE(TAG, "Invalid DevEUI hex string");
        return ESP_ERR_INVALID_ARG;
    }

    if (!hex_to_u64(join_eui_str, &join_eui)) {
        ESP_LOGE(TAG, "Invalid JoinEUI hex string");
        return ESP_ERR_INVALID_ARG;
    }

    if (!hex_to_bytes(app_key_str, app_key, 16)) {
        ESP_LOGE(TAG, "Invalid AppKey hex string");
        return ESP_ERR_INVALID_ARG;
    }

    // For LoRaWAN 1.0.x, NwkKey = AppKey
    memcpy(nwk_key, app_key, 16);

    xSemaphoreTake(lorawan_mutex, portMAX_DELAY);

    stats.last_join_attempt_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    stats.join_attempts++;

    ESP_LOGI(TAG, "Starting OTAA join (attempt %d)...", stats.join_attempts);
    ESP_LOGI(TAG, "DevEUI: %s", dev_eui_str);
    ESP_LOGI(TAG, "JoinEUI: %s", join_eui_str);

    // Begin OTAA provisioning
    node->beginOTAA(join_eui, dev_eui, nwk_key, app_key);

    // Attempt activation
    int state = node->activateOTAA();

    if (state == RADIOLIB_ERR_NONE) {
        stats.joined = true;
        stats.dev_addr = (uint32_t)node->getDevAddr();
        ESP_LOGI(TAG, "OTAA join successful! DevAddr: 0x%08lX", (unsigned long)stats.dev_addr);

        xSemaphoreGive(lorawan_mutex);
        return ESP_OK;
    } else {
        stats.joined = false;
        ESP_LOGE(TAG, "OTAA join failed, code %d", state);

        xSemaphoreGive(lorawan_mutex);
        return ESP_ERR_TIMEOUT;
    }
}

extern "C" esp_err_t lorawan_send(const uint8_t *data, size_t len, uint8_t port, bool confirmed)
{
    if (!initialized || !node || !stats.joined) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!data || len == 0 || len > 51) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(lorawan_mutex, portMAX_DELAY);

    // Set FPort
    node->setDutyCycle(true, 0);

    ESP_LOGI(TAG, "Sending uplink (%zu bytes, port %d, %s)...",
             len, port, confirmed ? "confirmed" : "unconfirmed");

    // Send uplink (blocking)
    int state;
    uint8_t downlink_data[256] = {0};
    size_t downlink_len = sizeof(downlink_data);

    if (confirmed) {
        state = node->sendReceive((uint8_t *)data, len, port, downlink_data, &downlink_len, true);
    } else {
        state = node->sendReceive((uint8_t *)data, len, port, downlink_data, &downlink_len);
    }

    if (state == RADIOLIB_ERR_NONE) {
        stats.uplink_count++;
        stats.last_uplink_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        ESP_LOGI(TAG, "Uplink #%lu sent successfully", (unsigned long)stats.uplink_count);

        // Check for downlink
        if (downlink_len > 0) {
            stats.downlink_count++;
            ESP_LOGI(TAG, "Downlink received (%zu bytes)", downlink_len);
        }
    } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
        // No downlink received, but uplink was sent
        stats.uplink_count++;
        stats.last_uplink_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        ESP_LOGD(TAG, "Uplink #%lu sent (no downlink)", (unsigned long)stats.uplink_count);
    } else {
        ESP_LOGE(TAG, "Uplink failed, code %d", state);

        // Check if we lost the session
        if (state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
            stats.joined = false;
            ESP_LOGW(TAG, "Session lost, need to re-join");
        }

        xSemaphoreGive(lorawan_mutex);
        return ESP_FAIL;
    }

    xSemaphoreGive(lorawan_mutex);
    return ESP_OK;
}

extern "C" bool lorawan_is_joined(void)
{
    return stats.joined;
}

extern "C" esp_err_t lorawan_get_stats(lorawan_stats_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(lorawan_mutex, portMAX_DELAY);
    memcpy(out, &stats, sizeof(lorawan_stats_t));
    xSemaphoreGive(lorawan_mutex);

    return ESP_OK;
}

extern "C" esp_err_t lorawan_force_rejoin(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(lorawan_mutex, portMAX_DELAY);
    stats.joined = false;
    stats.join_attempts = 0;
    xSemaphoreGive(lorawan_mutex);

    return lorawan_join();
}

extern "C" void lorawan_task(void *param)
{
    ESP_LOGI(TAG, "LoRaWAN task started");

    // Wait for system to settle
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Initialize the radio hardware
    if (lorawan_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LoRaWAN, task exiting");
        vTaskDelete(NULL);
        return;
    }

    // Check if credentials are configured
    const char *dev_eui = config_get_dev_eui();
    if (strlen(dev_eui) != 16) {
        ESP_LOGW(TAG, "LoRaWAN credentials not configured. Waiting for configuration via web UI...");

        // Wait until credentials are configured
        while (strlen(config_get_dev_eui()) != 16 ||
               strlen(config_get_join_eui()) != 16 ||
               strlen(config_get_app_key()) != 32) {
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
        ESP_LOGI(TAG, "Credentials detected, proceeding with join...");
    }

    // Join loop with exponential backoff
    uint32_t backoff_ms = 10000; // Start with 10s
    const uint32_t max_backoff_ms = 300000; // Max 5 minutes

    while (!lorawan_is_joined()) {
        esp_err_t ret = lorawan_join();
        if (ret == ESP_OK) {
            break;
        }

        ESP_LOGW(TAG, "Join failed, retrying in %lu ms...", (unsigned long)backoff_ms);
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));

        // Exponential backoff
        backoff_ms = (backoff_ms * 2 > max_backoff_ms) ? max_backoff_ms : backoff_ms * 2;
    }

    // Main uplink loop
    while (1) {
        uint32_t interval_s = config_get_uplink_interval();
        if (interval_s < 10) interval_s = 10;

        vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));

        if (!lorawan_is_joined()) {
            // Try to re-join
            ESP_LOGW(TAG, "Not joined, attempting re-join...");
            lorawan_join();
            continue;
        }

        // The actual payload construction and sending is done by the
        // sensor_task via lorawan_send(). This loop just manages the
        // session and handles re-joins if needed.
        //
        // If no external sender is configured, we send a heartbeat.
        // The main.c will wire sensor_task -> cayenne_lpp -> lorawan_send().
    }
}
