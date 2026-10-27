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
 * This file implements DISCOVER -> ANNOUNCE, READ -> READING, and the
 * provisioning handlers SET -> ACK, IDENTIFY -> ACK, COMMIT -> ACK.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_system.h"

#include "mbedtls/gcm.h"
#include "mbedtls/base64.h"

#include "cJSON.h"

#include "config_manager.h"
#include "sensor_manager.h"
#include "buzzer.h"
#include "factory_key.h"
#include "espnow_setup.h"

#define ESPNOW_CHANNEL      1
#define PROTOCOL_MAGIC      "LTCM"
#define PROTOCOL_VERSION    1
#define MAX_LINE_LEN         250   /* DISCOVER/READ replies stay well under this */

/*
 * ESP-NOW V2 (ESP-IDF >= v5.4, so v5.5.3) transparently sends/receives frames
 * up to ESP_NOW_MAX_DATA_LEN_V2 (1470 B) with no enable step: there is no
 * Kconfig, no per-peer version field, and no version setter API (only the
 * read-only esp_now_get_version()). A device compiled against v5.5.3 IS a v2.0
 * peer, so the encrypted SET frame (which exceeds the old 250 B V1 limit for
 * real WiFi creds) transmits fine between the dongle and the device.
 *
 * MAX_FRAME_LEN bounds the frame size we ACCEPT and the work-buffer sizes. Real
 * SET frames (base64(nonce|ct|tag) inside JSON, with SSID<=32 / pass<=64 / name)
 * are well under 1 KB, so this stays far below the V2 ceiling while covering
 * every protocol message. Frames larger than this are dropped.
 */
#define MAX_FRAME_LEN        1024
#define RX_QUEUE_DEPTH       4

/* A received ESP-NOW frame, copied out of the WiFi-task recv callback and
 * handed to the espnow_setup_run loop task, which does the heavy work
 * (base64 + GCM + cJSON + config apply + ACK) off the WiFi task stack. */
typedef struct {
    uint8_t src[6];
    int     len;
    uint8_t data[MAX_FRAME_LEN];
} setup_rx_msg_t;

static const char *TAG = "SETUP";
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static QueueHandle_t s_rx_queue;

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

static void get_own_mac_str(char *out /* >= 18 bytes */)
{
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    mac_to_str(mac, out);
}

static int get_int(cJSON *obj, const char *key, int def)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : def;
}

/**
 * @brief Send ACK {m,v,ty:"ACK", mac, ok, err?, seq} to @p dst.
 */
static void send_ack(const uint8_t *dst, const char *own_mac_str, bool ok,
                      const char *err, int seq)
{
    cJSON *root = new_envelope("ACK");
    cJSON_AddStringToObject(root, "mac", own_mac_str);
    cJSON_AddBoolToObject(root, "ok", ok);
    if (err) {
        cJSON_AddStringToObject(root, "err", err);
    }
    cJSON_AddNumberToObject(root, "seq", seq);
    send_line(dst, root);
}

/* ---------------------------------------------------------------------- */
/* AES-128-GCM decrypt helper                                             */
/* ---------------------------------------------------------------------- */

/**
 * @brief Decrypt base64(nonce(12) || ciphertext || tag(16)) with FACTORY_KEY.
 * @return 0 on success (base64 decode, auth, and decrypt all ok); -1 otherwise.
 */
static int gcm_open(const char *b64, uint8_t *out, size_t out_cap, size_t *out_len)
{
    /* Heap-allocate the base64-decode scratch so this decrypt path carries no
     * large stack frame on the loop task (frames are bounded by MAX_FRAME_LEN,
     * and base64 decodes to fewer bytes than its input). */
    uint8_t *raw = malloc(MAX_FRAME_LEN);
    if (!raw) {
        return -1;
    }
    size_t raw_len = 0;
    if (mbedtls_base64_decode(raw, MAX_FRAME_LEN, &raw_len,
            (const uint8_t *)b64, strlen(b64)) != 0 ||
        raw_len < 12 + 16) {
        free(raw);
        return -1;
    }
    const uint8_t *nonce = raw;
    const uint8_t *ct = raw + 12;
    size_t ct_len = raw_len - 12 - 16;
    const uint8_t *tag = raw + 12 + ct_len;
    if (ct_len > out_cap) {
        free(raw);
        return -1;
    }

    mbedtls_gcm_context g;
    mbedtls_gcm_init(&g);
    int rc = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, FACTORY_KEY, 128);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&g, ct_len, nonce, 12, NULL, 0,
                                       tag, 16, ct, out);
    }
    mbedtls_gcm_free(&g);
    free(raw);
    if (rc == 0) {
        *out_len = ct_len;
        return 0;
    }
    return -1;
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

/**
 * @brief SET: decrypt an AES-128-GCM payload and apply calibration/identity.
 *
 * The plaintext JSON is {mac, tcorr, hcorr, tccorr, name, ssid, pass}. If the
 * decrypted mac doesn't match this device's own STA MAC, the message wasn't
 * meant for us: it is silently ignored (no ACK at all). Any other failure
 * (bad base64, decrypt/auth failure, bad JSON) is ACKed with ok:false + err.
 */
static void handle_set(cJSON *req, const uint8_t *src)
{
    int seq = get_int(req, "seq", 0);
    char own_mac_str[18];
    get_own_mac_str(own_mac_str);

    cJSON *enc = cJSON_GetObjectItem(req, "enc");
    if (!cJSON_IsString(enc) || !enc->valuestring) {
        send_ack(src, own_mac_str, false, "missing enc", seq);
        return;
    }

    /* Heap the plaintext buffer too: keeps the loop-task stack small and lets
     * the plaintext be as large as any accepted frame. */
    uint8_t *plain = malloc(MAX_FRAME_LEN);
    if (!plain) {
        send_ack(src, own_mac_str, false, "no mem", seq);
        return;
    }
    size_t plain_len = 0;
    if (gcm_open(enc->valuestring, plain, MAX_FRAME_LEN - 1, &plain_len) != 0) {
        send_ack(src, own_mac_str, false, "decrypt failed", seq);
        free(plain);
        return;
    }
    plain[plain_len] = '\0';

    cJSON *payload = cJSON_ParseWithLength((const char *)plain, plain_len);
    free(plain);
    if (!payload) {
        send_ack(src, own_mac_str, false, "bad json", seq);
        return;
    }

    cJSON *pmac = cJSON_GetObjectItem(payload, "mac");
    if (!cJSON_IsString(pmac) || !pmac->valuestring ||
        strcmp(pmac->valuestring, own_mac_str) != 0) {
        /* Not addressed to this device: ignore silently, no ACK. */
        cJSON_Delete(payload);
        return;
    }

    cJSON *tcorr = cJSON_GetObjectItem(payload, "tcorr");
    cJSON *hcorr = cJSON_GetObjectItem(payload, "hcorr");
    cJSON *tccorr = cJSON_GetObjectItem(payload, "tccorr");
    cJSON *name = cJSON_GetObjectItem(payload, "name");
    cJSON *ssid = cJSON_GetObjectItem(payload, "ssid");
    cJSON *pass = cJSON_GetObjectItem(payload, "pass");

    if (cJSON_IsNumber(tcorr)) {
        config_set_temp_correction((float)tcorr->valuedouble);
    }
    if (cJSON_IsNumber(hcorr)) {
        config_set_hum_correction((float)hcorr->valuedouble);
    }
    if (cJSON_IsNumber(tccorr)) {
        config_set_thermocouple_correction((float)tccorr->valuedouble);
    }
    if (cJSON_IsString(name) && name->valuestring) {
        config_set_device_name(name->valuestring);
    }
    if (cJSON_IsString(ssid) && ssid->valuestring && ssid->valuestring[0] != '\0') {
        config_set_wifi_ssid(ssid->valuestring);
        config_set_wifi_password((cJSON_IsString(pass) && pass->valuestring) ? pass->valuestring : "");
        config_set_wifi_ap_mode(false);
    }

    cJSON_Delete(payload);

    if (config_save() != ESP_OK) {
        send_ack(src, own_mac_str, false, "config_save failed", seq);
        return;
    }

    send_ack(src, own_mac_str, true, NULL, seq);
}

/**
 * @brief IDENTIFY: beep the buzzer when the target mac matches our own.
 */
static void handle_identify(cJSON *req, const uint8_t *src)
{
    int seq = get_int(req, "seq", 0);
    char own_mac_str[18];
    get_own_mac_str(own_mac_str);

    cJSON *mac = cJSON_GetObjectItem(req, "mac");
    if (!cJSON_IsString(mac) || !mac->valuestring ||
        strcmp(mac->valuestring, own_mac_str) != 0) {
        return;
    }

    buzzer_beep(300);
    send_ack(src, own_mac_str, true, NULL, seq);
}

/**
 * @brief COMMIT: mark this device as factory-provisioned, ACK, then reboot.
 */
static void handle_commit(cJSON *req, const uint8_t *src)
{
    int seq = get_int(req, "seq", 0);
    char own_mac_str[18];
    get_own_mac_str(own_mac_str);

    cJSON *mac = cJSON_GetObjectItem(req, "mac");
    if (!cJSON_IsString(mac) || !mac->valuestring ||
        strcmp(mac->valuestring, own_mac_str) != 0) {
        return;
    }

    config_set_factory_provisioned(true);
    if (config_save() != ESP_OK) {
        send_ack(src, own_mac_str, false, "save failed", seq);
        return;
    }

    send_ack(src, own_mac_str, true, NULL, seq);

    /* Let the ACK flush over the air before rebooting. */
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

/* ---------------------------------------------------------------------- */
/* ESP-NOW plumbing                                                        */
/* ---------------------------------------------------------------------- */

/**
 * @brief Ensure @p mac is a registered ESP-NOW peer before we try to reply to
 * it.
 *
 * wifi_espnow_init() only registers the broadcast peer up front; the dongle's
 * unicast MAC is never added. esp_now_send() to a MAC that is not a
 * registered peer returns ESP_ERR_ESPNOW_NOT_FOUND and silently drops the
 * frame, so every reply (ANNOUNCE/READING/ACK) would otherwise be lost even
 * though DISCOVER/READ/SET/IDENTIFY/COMMIT are received fine (RX needs no
 * peer). Learn the sender as a peer lazily, the first time we need to answer
 * it.
 */
static void ensure_unicast_peer(const uint8_t *mac)
{
    if (memcmp(mac, BCAST, 6) == 0) {
        return; /* broadcast peer already registered in wifi_espnow_init() */
    }
    if (esp_now_is_peer_exist(mac)) {
        return;
    }
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_err_t rc = esp_now_add_peer(&peer);
    if (rc != ESP_OK) {
        /* Defensive only: there is normally exactly one dongle, so the
         * 20-peer cap (ESP_ERR_ESPNOW_FULL) should never actually trigger. */
        ESP_LOGW(TAG, "add reply peer failed: %s", esp_err_to_name(rc));
    }
}

/* Dispatch one received frame. Runs on the espnow_setup_run loop task (NOT the
 * WiFi task) so the base64/GCM/cJSON/config work has a normal-sized stack. */
static void handle_frame(const uint8_t *data, int len, const uint8_t *src)
{
    /* Register src as a peer BEFORE any handler can reply, so ANNOUNCE,
     * READING, and every ACK go out to a registered peer. */
    ensure_unicast_peer(src);

    cJSON *root = cJSON_ParseWithLength((const char *)data, (size_t)len);
    if (!root) {
        return;
    }

    cJSON *ty = cJSON_GetObjectItem(root, "ty");
    if (cJSON_IsString(ty) && ty->valuestring) {
        if (strcmp(ty->valuestring, "DISCOVER") == 0) {
            handle_discover(src);
        } else if (strcmp(ty->valuestring, "READ") == 0) {
            handle_read(src);
        } else if (strcmp(ty->valuestring, "SET") == 0) {
            handle_set(root, src);
        } else if (strcmp(ty->valuestring, "IDENTIFY") == 0) {
            handle_identify(root, src);
        } else if (strcmp(ty->valuestring, "COMMIT") == 0) {
            handle_commit(root, src);
        }
    }

    cJSON_Delete(root);
}

/* ESP-NOW recv callback: runs in the small WiFi task stack, so it does the
 * minimum possible -- copy the frame + source MAC into a queue and return.
 * All heavy work happens on the loop task in handle_frame(). */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len <= 0 || len > MAX_FRAME_LEN) {
        if (info && data && len > MAX_FRAME_LEN) {
            ESP_LOGW(TAG, "dropping oversize frame (%d > %d)", len, MAX_FRAME_LEN);
        }
        return;
    }
    if (!s_rx_queue) {
        return;
    }

    setup_rx_msg_t msg;
    memcpy(msg.src, info->src_addr, 6);
    msg.len = len;
    memcpy(msg.data, data, (size_t)len);
    if (xQueueSend(s_rx_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "rx queue full, dropping frame");
    }
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
    /* A dirty/aged NVS partition must not panic-loop the setup path: erase and
     * retry once, mirroring the pattern in main/wifi/wifi_manager.c. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    /* Queue must exist before the recv callback can fire. */
    s_rx_queue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(setup_rx_msg_t));
    ESP_ERROR_CHECK(s_rx_queue ? ESP_OK : ESP_ERR_NO_MEM);

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

    /* Statically size the drain buffer; it is large (setup_rx_msg_t holds a
     * MAX_FRAME_LEN payload) so keep it off this task's stack. */
    static setup_rx_msg_t rx;
    TickType_t last_read = xTaskGetTickCount();
    const TickType_t read_period = pdMS_TO_TICKS(2000);

    while (1) {
        /* Drain received frames promptly (heavy work runs here, off the WiFi
         * task). Block up to read_period so sensor data stays fresh when idle. */
        if (xQueueReceive(s_rx_queue, &rx, read_period) == pdTRUE) {
            handle_frame(rx.data, rx.len, rx.src);
        }

        if (xTaskGetTickCount() - last_read >= read_period) {
            sensor_manager_read();
            last_read = xTaskGetTickCount();
        }
    }
}
