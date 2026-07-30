/**
 * @file espnow_setup.c
 * @brief ESP-NOW factory setup-mode responder
 *
 * Runs a minimal ESP-NOW protocol responder on WiFi STA channel 1. A PC-side
 * factory-setup app talks to this device via a USB "dongle" that bridges
 * serial <-> ESP-NOW. The wire protocol is newline-delimited JSON:
 *
 *   {"m":"LTCM","v":1,"ty":<TYPE>, ...}
 *
 * This file implements DISCOVER -> ANNOUNCE and READ -> READING. SET,
 * IDENTIFY and COMMIT are added in a follow-up task.
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"

#include "cJSON.h"

#include "config_manager.h"
#include "sensor_manager.h"
#include "espnow_setup.h"

#define ESPNOW_CHANNEL      1
#define PROTOCOL_MAGIC      "LTCM"
#define PROTOCOL_VERSION    1
#define MAX_LINE_LEN         250   /* DISCOVER/READ replies stay well under this */

static const char *TAG = "SETUP";
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */
/* ---------------------------------------------------------------------- */

static void mac_to_str(const uint8_t mac[6], char *out /* >= 18 bytes */)
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief Serialize @p root compactly, append '\n', send to @p dst, and free.
 */
static void send_line(const uint8_t *dst, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        size_t len = strlen(json);
        if (len + 1 < MAX_LINE_LEN) {
            char buf[MAX_LINE_LEN];
            memcpy(buf, json, len);
            buf[len] = '\n';
            esp_err_t err = esp_now_send(dst, (const uint8_t *)buf, len + 1);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "esp_now_send failed: %d", err);
            }
        } else {
            ESP_LOGW(TAG, "message too large (%d bytes), dropped", (int)len);
        }
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

static cJSON *new_envelope(const char *ty)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "m", PROTOCOL_MAGIC);
    cJSON_AddNumberToObject(root, "v", PROTOCOL_VERSION);
    cJSON_AddStringToObject(root, "ty", ty);
    return root;
}

/* ---------------------------------------------------------------------- */
/* Handlers                                                                */
/* ---------------------------------------------------------------------- */

static void handle_discover(const uint8_t *src)
{
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char mac_str[18];
    mac_to_str(mac, mac_str);

    sensor_data_t d = {0};
    sensor_manager_get_data(&d);

    cJSON *root = new_envelope("ANNOUNCE");
    cJSON_AddStringToObject(root, "mac", mac_str);
    cJSON_AddStringToObject(root, "deveui", config_get_dev_eui());
    cJSON_AddStringToObject(root, "fw", "setup");
    cJSON_AddStringToObject(root, "sensor", d.sensor_name);
    cJSON_AddBoolToObject(root, "prov", config_get_factory_provisioned());

    send_line(src, root);
}

static void handle_read(const uint8_t *src)
{
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    char mac_str[18];
    mac_to_str(mac, mac_str);

    sensor_data_t d = {0};
    sensor_manager_get_data(&d);

    cJSON *root = new_envelope("READING");
    cJSON_AddStringToObject(root, "mac", mac_str);
    cJSON_AddNumberToObject(root, "t", d.temperature);
    cJSON_AddNumberToObject(root, "h", d.humidity);
    if (d.thermocouple_valid) {
        cJSON_AddNumberToObject(root, "tc", d.thermocouple_temp);
    } else {
        cJSON_AddNullToObject(root, "tc");
    }
    /* Applied corrections, so the PC app can recover the raw reading by
     * subtracting them back out. */
    cJSON_AddNumberToObject(root, "tcorr", config_get_temp_correction());
    cJSON_AddNumberToObject(root, "hcorr", config_get_hum_correction());
    cJSON_AddNumberToObject(root, "tccorr", config_get_thermocouple_correction());
    cJSON_AddBoolToObject(root, "tvalid", d.temp_hum_valid);
    cJSON_AddBoolToObject(root, "hvalid", d.temp_hum_valid);
    cJSON_AddBoolToObject(root, "tcvalid", d.thermocouple_valid);

    send_line(src, root);
}

/* ---------------------------------------------------------------------- */
/* ESP-NOW plumbing                                                        */
/* ---------------------------------------------------------------------- */

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len <= 0) {
        return;
    }

    cJSON *root = cJSON_ParseWithLength((const char *)data, (size_t)len);
    if (!root) {
        return;
    }

    cJSON *ty = cJSON_GetObjectItem(root, "ty");
    if (cJSON_IsString(ty) && ty->valuestring) {
        if (strcmp(ty->valuestring, "DISCOVER") == 0) {
            handle_discover(info->src_addr);
        } else if (strcmp(ty->valuestring, "READ") == 0) {
            handle_read(info->src_addr);
        }
        /* SET / IDENTIFY / COMMIT are handled by a follow-up task. */
    }

    cJSON_Delete(root);
}

static void add_broadcast_peer(void)
{
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, BCAST, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
}

static void wifi_espnow_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    add_broadcast_peer();
}

/* ---------------------------------------------------------------------- */
/* Public entry point                                                      */
/* ---------------------------------------------------------------------- */

void espnow_setup_run(void)
{
    wifi_espnow_init();

    /* Make sure the sensor manager is up and has at least one reading
     * available before any DISCOVER/READ request comes in. */
    sensor_manager_init();
    sensor_manager_read();

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "setup mode ready, ch=%d mac=%02X:%02X:%02X:%02X:%02X:%02X",
             ESPNOW_CHANNEL, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    while (1) {
        /* Keep sensor data reasonably fresh for READ requests; the actual
         * request handling happens in the esp_now recv callback. */
        sensor_manager_read();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
