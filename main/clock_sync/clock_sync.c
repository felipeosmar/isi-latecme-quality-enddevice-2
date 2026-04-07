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

#define GPS_TO_UNIX_OFFSET  315964800UL
#define MAX_CORRECTION_S    (365 * 24 * 3600)

// LoRaWAN TS003 constants — defined here to avoid including a C++ header
// Values from managed_components/jgromes__radiolib/src/protocols/LoRaWAN/LoRaWAN.h
#define CLOCK_SYNC_FPORT        202   // RADIOLIB_LORAWAN_FPORT_TS003
#define CLOCK_SYNC_PACKAGE_ID     1   // RADIOLIB_LORAWAN_PACKAGE_TS003

static volatile bool s_synced = false;
static volatile uint32_t s_device_time_sent = 0;

static void on_apptime_ans(uint8_t *data, size_t len)
{
    // AppTimeAns format (RadioLib passes full payload, CID included):
    // data[0] = CID (0x01), data[1..4] = TimeCorrection (signed LE, GPS s), data[5] = TokenAns
    if (len < 6) {
        ESP_LOGW(TAG, "AppTimeAns too short: %zu bytes (expected 6)", len);
        return;
    }

    int32_t correction = 0;
    memcpy(&correction, data + 1, sizeof(int32_t));  // skip CID byte

    // Only validate range on re-syncs (s_synced=true). On the first sync,
    // DeviceTime=0 so correction ≈ current GPS time (~1.45B s) — well above 1 year.
    if (s_synced && (correction > MAX_CORRECTION_S || correction < -MAX_CORRECTION_S)) {
        ESP_LOGW(TAG, "TimeCorrection out of range: %ld s — ignoring", (long)correction);
        return;
    }

    time_t unix_time = (time_t)s_device_time_sent + (time_t)correction + (time_t)GPS_TO_UNIX_OFFSET;

    struct timeval tv = { .tv_sec = unix_time, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    s_synced = true;

    struct tm timeinfo;
    gmtime_r(&unix_time, &timeinfo);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "Clock synced to %s UTC (correction: %lds)", time_str, (long)correction);
}

esp_err_t clock_sync_init(void)
{
    s_synced = false;
    s_device_time_sent = 0;
    ESP_LOGI(TAG, "Clock sync module initialized");
    return ESP_OK;
}

esp_err_t clock_sync_request(void)
{
    // TS003 AppTimeReq: CID(1) | DeviceTime(4 LE) | Param(1)
    // Param: bit0 = AnsRequired, bits[4:1] = TokenReq (unused, set to 0)
    uint8_t req[6];

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

    req[0] = CLOCK_SYNC_PACKAGE_ID;              // CID = 0x01 (AppTimeReq)
    req[1] = (uint8_t)(device_time & 0xFF);
    req[2] = (uint8_t)((device_time >> 8) & 0xFF);
    req[3] = (uint8_t)((device_time >> 16) & 0xFF);
    req[4] = (uint8_t)((device_time >> 24) & 0xFF);
    req[5] = 0x01;                                // AnsRequired = 1, TokenReq = 0

    ESP_LOGI(TAG, "Sending AppTimeReq (DeviceTime=%lu)", (unsigned long)device_time);

    esp_err_t ret = lorawan_send(req, sizeof(req), CLOCK_SYNC_FPORT, false);  // 6 bytes
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

    while (!lorawan_is_joined()) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    esp_err_t ret = lorawan_add_app_package(CLOCK_SYNC_PACKAGE_ID, on_apptime_ans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register TS003 package: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    // Initial sync with up to 3 retries (60s apart) before falling back to daily cycle
    for (int attempt = 0; attempt < 3 && !s_synced; attempt++) {
        if (attempt > 0) {
            ESP_LOGI(TAG, "Retry %d/3 in 60s...", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
        clock_sync_request();
    }

    if (!s_synced) {
        ESP_LOGW(TAG, "Initial sync failed after 3 attempts, will retry in 24h");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(24UL * 3600UL * 1000UL));
        ESP_LOGI(TAG, "Daily re-sync triggered");
        clock_sync_request();
    }
}
