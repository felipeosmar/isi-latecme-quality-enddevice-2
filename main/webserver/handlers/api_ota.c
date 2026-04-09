/**
 * @file api_ota.c
 * @brief OTA firmware and www partition update handlers
 */

#include "handlers.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "auto_updater.h"

static const char *TAG = "OTA";

#define OTA_CHUNK_SIZE 4096

// ============================================================================
// OTA State
// ============================================================================

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_IN_PROGRESS,
    OTA_STATE_REBOOTING,
    OTA_STATE_FAILED
} ota_state_t;

typedef struct {
    ota_state_t state;
    uint32_t bytes_written;
    uint32_t total_bytes;
    char error_msg[64];
} ota_status_t;

static ota_status_t s_ota = {
    .state = OTA_STATE_IDLE,
    .bytes_written = 0,
    .total_bytes = 0,
    .error_msg = ""
};

// Cached once at first call — esp_ota_check_rollback_is_possible() uses
// bootloader_mmap internally and is not re-entrant; concurrent HTTP requests
// to /api/ota/status would otherwise trigger "tried to bootloader_mmap twice".
static int s_rollback_possible = -1;  // -1 = not yet evaluated

static bool get_rollback_possible(void)
{
    if (s_rollback_possible == -1) {
        s_rollback_possible = esp_ota_check_rollback_is_possible() ? 1 : 0;
    }
    return s_rollback_possible == 1;
}

// ============================================================================
// GET /api/ota/status
// ============================================================================

esp_err_t api_ota_status_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *app_desc = esp_app_get_description();

    const char *state_str = "idle";
    if (s_ota.state == OTA_STATE_IN_PROGRESS) state_str = "in_progress";
    else if (s_ota.state == OTA_STATE_REBOOTING) state_str = "rebooting";
    else if (s_ota.state == OTA_STATE_FAILED) state_str = "failed";

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", state_str);
    cJSON_AddStringToObject(root, "current_partition", running ? running->label : "unknown");
    cJSON_AddStringToObject(root, "app_version", app_desc ? app_desc->version : "unknown");
    cJSON_AddStringToObject(root, "idf_version", app_desc ? app_desc->idf_ver : "unknown");
    cJSON_AddBoolToObject(root, "rollback_possible", get_rollback_possible());
    cJSON_AddNumberToObject(root, "bytes_written", s_ota.bytes_written);
    cJSON_AddNumberToObject(root, "total_bytes", s_ota.total_bytes);
    if (s_ota.state == OTA_STATE_FAILED) {
        cJSON_AddStringToObject(root, "error", s_ota.error_msg);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// ============================================================================
// POST /api/ota/firmware/upload
// Streams binary chunks directly to OTA partition — no heap buffering.
// ============================================================================

esp_err_t api_ota_firmware_upload_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    if (s_ota.state == OTA_STATE_IN_PROGRESS) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA already in progress");
        return ESP_FAIL;
    }

    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition available");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "%s", esp_err_to_name(err));
        s_ota.state = OTA_STATE_FAILED;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, s_ota.error_msg);
        return ESP_FAIL;
    }

    char *buf = malloc(OTA_CHUNK_SIZE);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    s_ota.state = OTA_STATE_IN_PROGRESS;
    s_ota.bytes_written = 0;
    s_ota.total_bytes = req->content_len;
    s_ota.error_msg[0] = '\0';

    int remaining = req->content_len;
    bool ota_error = false;

    while (remaining > 0) {
        int to_read = remaining < OTA_CHUNK_SIZE ? remaining : OTA_CHUNK_SIZE;
        int received = httpd_req_recv(req, buf, to_read);
        if (received <= 0) {
            ESP_LOGE(TAG, "recv failed at %lu/%lu bytes", s_ota.bytes_written, s_ota.total_bytes);
            ota_error = true;
            break;
        }
        err = esp_ota_write(ota_handle, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            ota_error = true;
            break;
        }
        remaining -= received;
        s_ota.bytes_written += received;
    }

    free(buf);

    if (ota_error) {
        esp_ota_abort(ota_handle);
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "Transfer failed at %lu bytes", s_ota.bytes_written);
        s_ota.state = OTA_STATE_FAILED;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, s_ota.error_msg);
        return ESP_FAIL;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "%s", esp_err_to_name(err));
        s_ota.state = OTA_STATE_FAILED;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, s_ota.error_msg);
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "%s", esp_err_to_name(err));
        s_ota.state = OTA_STATE_FAILED;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, s_ota.error_msg);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Firmware OTA complete (%lu bytes), rebooting to %s", s_ota.bytes_written, update_partition->label);
    s_ota.state = OTA_STATE_REBOOTING;

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", "OTA complete, rebooting");
    cJSON_AddStringToObject(resp, "partition", update_partition->label);
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(resp);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// ============================================================================
// POST /api/ota/firmware/url
// Launches a background task to download and flash from a URL.
// ============================================================================

typedef struct {
    char url[512];
} ota_url_params_t;

static void ota_url_task(void *pvParameters)
{
    ota_url_params_t *params = (ota_url_params_t *)pvParameters;

    ESP_LOGI(TAG, "OTA URL task started: %s", params->url);

    esp_http_client_config_t http_config = {
        .url                = params->url,
        .timeout_ms         = 60000,
        .buffer_size        = OTA_CHUNK_SIZE,
        .buffer_size_tx     = 2048,
        .crt_bundle_attach  = esp_crt_bundle_attach,
        .max_redirection_count = 5,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "HTTP client init failed");
        s_ota.state = OTA_STATE_FAILED;
        free(params);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "HTTP open failed");
        s_ota.state = OTA_STATE_FAILED;
        esp_http_client_cleanup(client);
        free(params);
        vTaskDelete(NULL);
        return;
    }

    // Follow redirects — GitHub release assets redirect (302) to CDN
    int content_length = 0;
    int status = 0;
    for (int redir = 0; redir <= 5; redir++) {
        content_length = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status < 300 || status >= 400) break;
        ESP_LOGI(TAG, "URL OTA HTTP %d redirect (hop %d)", status, redir + 1);
        esp_http_client_close(client);
        if (esp_http_client_set_redirection(client) != ESP_OK || redir == 5) {
            ESP_LOGE(TAG, "Redirect failed or limit reached");
            snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "Redirect failed");
            s_ota.state = OTA_STATE_FAILED;
            esp_http_client_cleanup(client);
            free(params);
            vTaskDelete(NULL);
            return;
        }
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP open (redirect) failed: %s", esp_err_to_name(err));
            snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "Redirect open failed");
            s_ota.state = OTA_STATE_FAILED;
            esp_http_client_cleanup(client);
            free(params);
            vTaskDelete(NULL);
            return;
        }
    }
    if (status != 200) {
        ESP_LOGE(TAG, "URL OTA HTTP %d", status);
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "HTTP %d", status);
        s_ota.state = OTA_STATE_FAILED;
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(params);
        vTaskDelete(NULL);
        return;
    }
    if (content_length < 0) content_length = 0;
    s_ota.total_bytes = (uint32_t)content_length;

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition available");
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "No OTA partition");
        s_ota.state = OTA_STATE_FAILED;
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(params);
        vTaskDelete(NULL);
        return;
    }

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "%s", esp_err_to_name(err));
        s_ota.state = OTA_STATE_FAILED;
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(params);
        vTaskDelete(NULL);
        return;
    }

    char *buf = malloc(OTA_CHUNK_SIZE);
    if (!buf) {
        esp_ota_abort(ota_handle);
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "Out of memory");
        s_ota.state = OTA_STATE_FAILED;
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(params);
        vTaskDelete(NULL);
        return;
    }

    bool ota_error = false;
    s_ota.bytes_written = 0;
    int last_logged_pct = -1;

    ESP_LOGI(TAG, "URL OTA download started (%lu bytes)", s_ota.total_bytes);

    while (1) {
        int data_read = esp_http_client_read(client, buf, OTA_CHUNK_SIZE);
        if (data_read < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            ota_error = true;
            break;
        }
        if (data_read == 0) break;

        err = esp_ota_write(ota_handle, buf, data_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            ota_error = true;
            break;
        }
        s_ota.bytes_written += data_read;

        if (s_ota.total_bytes > 0) {
            int pct = (int)((s_ota.bytes_written * 100) / s_ota.total_bytes);
            if (pct / 10 > last_logged_pct / 10) {
                ESP_LOGI(TAG, "URL OTA progress: %d%% (%lu / %lu bytes)",
                         pct, s_ota.bytes_written, s_ota.total_bytes);
                last_logged_pct = pct;
            }
        }
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(params);

    if (ota_error) {
        esp_ota_abort(ota_handle);
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "Download or write failed");
        s_ota.state = OTA_STATE_FAILED;
        vTaskDelete(NULL);
        return;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "ota_end: %s", esp_err_to_name(err));
        s_ota.state = OTA_STATE_FAILED;
        vTaskDelete(NULL);
        return;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        snprintf(s_ota.error_msg, sizeof(s_ota.error_msg), "set_boot: %s", esp_err_to_name(err));
        s_ota.state = OTA_STATE_FAILED;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "URL OTA complete (%lu bytes), rebooting to %s", s_ota.bytes_written, update_partition->label);
    s_ota.state = OTA_STATE_REBOOTING;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    /* unreachable */
}

esp_err_t api_ota_firmware_url_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    if (s_ota.state == OTA_STATE_IN_PROGRESS) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA already in progress");
        return ESP_FAIL;
    }

    char body[600];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *url_item = cJSON_GetObjectItem(json, "url");
    if (!url_item || !cJSON_IsString(url_item) || strlen(url_item->valuestring) == 0) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'url' field");
        return ESP_FAIL;
    }

    ota_url_params_t *params = malloc(sizeof(ota_url_params_t));
    if (!params) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    strncpy(params->url, url_item->valuestring, sizeof(params->url) - 1);
    params->url[sizeof(params->url) - 1] = '\0';
    cJSON_Delete(json);

    s_ota.bytes_written = 0;
    s_ota.total_bytes = 0;
    s_ota.error_msg[0] = '\0';
    s_ota.state = OTA_STATE_IN_PROGRESS;

    BaseType_t ret = xTaskCreate(ota_url_task, "ota_url", 8192, params, 5, NULL);
    if (ret != pdPASS) {
        free(params);
        s_ota.state = OTA_STATE_FAILED;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start OTA task");
        return ESP_FAIL;
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", "OTA started, poll /api/ota/status for progress");
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ============================================================================
// POST /api/ota/www/upload
// Unmounts LittleFS, erases www partition, writes binary, reboots.
// ============================================================================

esp_err_t api_ota_www_upload_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    const esp_partition_t *www_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    if (!www_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "www partition not found");
        return ESP_FAIL;
    }

    if (req->content_len > www_partition->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too large for www partition");
        return ESP_FAIL;
    }

    // Unmount www so we can write to it
    esp_vfs_littlefs_unregister("www");

    esp_err_t err = esp_partition_erase_range(www_partition, 0, www_partition->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "www erase failed: %s", esp_err_to_name(err));
        esp_vfs_littlefs_conf_t www_conf = { .base_path = "/www", .partition_label = "www", .format_if_mount_failed = false };
        esp_err_t remount_err = esp_vfs_littlefs_register(&www_conf);
        if (remount_err != ESP_OK) {
            ESP_LOGE(TAG, "www remount failed: %s", esp_err_to_name(remount_err));
        }
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erase failed");
        return ESP_FAIL;
    }

    char *buf = malloc(OTA_CHUNK_SIZE);
    if (!buf) {
        esp_vfs_littlefs_conf_t www_conf = { .base_path = "/www", .partition_label = "www", .format_if_mount_failed = false };
        esp_err_t remount_err = esp_vfs_littlefs_register(&www_conf);
        if (remount_err != ESP_OK) {
            ESP_LOGE(TAG, "www remount failed: %s", esp_err_to_name(remount_err));
        }
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    uint32_t offset = 0;
    bool write_error = false;

    while (remaining > 0) {
        int to_read = remaining < OTA_CHUNK_SIZE ? remaining : OTA_CHUNK_SIZE;
        int received = httpd_req_recv(req, buf, to_read);
        if (received <= 0) {
            write_error = true;
            break;
        }

        // esp_partition_write requires 4-byte aligned size
        int write_size = (received + 3) & ~3;
        if (write_size > received) {
            memset(buf + received, 0xFF, write_size - received);
        }

        err = esp_partition_write(www_partition, offset, buf, write_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "www write failed at offset %lu: %s", offset, esp_err_to_name(err));
            write_error = true;
            break;
        }
        offset += received;
        remaining -= received;
    }

    free(buf);

    if (write_error) {
        esp_vfs_littlefs_conf_t www_conf = { .base_path = "/www", .partition_label = "www", .format_if_mount_failed = false };
        esp_err_t remount_err = esp_vfs_littlefs_register(&www_conf);
        if (remount_err != ESP_OK) {
            ESP_LOGE(TAG, "www remount failed: %s", esp_err_to_name(remount_err));
        }
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "www partition updated (%lu bytes written), rebooting", offset);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", "www updated, rebooting");
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(resp);

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ============================================================================
// POST /api/ota/rollback
// ============================================================================

esp_err_t api_ota_rollback_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    if (!esp_ota_check_rollback_is_possible()) {
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", false);
        cJSON_AddStringToObject(resp, "message", "Rollback not available");
        char *json_str = cJSON_PrintUnformatted(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
        httpd_resp_send(req, json_str, strlen(json_str));
        free(json_str);
        cJSON_Delete(resp);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Manual rollback requested");

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", "Rolling back to previous firmware");
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(resp);

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_ota_mark_app_invalid_rollback_and_reboot();

    return ESP_OK;
}

// ============================================================================
// GET /api/ota/auto-update
// ============================================================================

esp_err_t api_ota_auto_update_get_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    const char *result_str = "never";
    switch (auto_updater_get_last_result()) {
        case AUTO_UPDATE_RESULT_UP_TO_DATE: result_str = "up_to_date"; break;
        case AUTO_UPDATE_RESULT_UPDATED:    result_str = "updated";    break;
        case AUTO_UPDATE_RESULT_ERROR:      result_str = "error";      break;
        default: break;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root,   "enabled",       config_get_auto_update_enabled());
    cJSON_AddStringToObject(root, "branch",        config_get_auto_update_branch());
    cJSON_AddStringToObject(root, "firmware_tag",  config_get_auto_update_firmware_tag());
    cJSON_AddStringToObject(root, "www_tag",       config_get_auto_update_www_tag());
    cJSON_AddNumberToObject(root, "last_check_time", (double)auto_updater_get_last_check_time());
    cJSON_AddStringToObject(root, "last_check_result", result_str);
    cJSON_AddBoolToObject(root,   "is_checking",      auto_updater_is_checking());
    cJSON_AddStringToObject(root, "last_run_log",     auto_updater_get_last_run_log());

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// ============================================================================
// POST /api/ota/auto-update
// Body: { "enabled": true, "branch": "main" }
// Optionally: { "trigger_now": true } to force an immediate check
// ============================================================================

esp_err_t api_ota_auto_update_post_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    char body[256];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *item;
    if ((item = cJSON_GetObjectItem(json, "enabled")) && cJSON_IsBool(item)) {
        config_set_auto_update_enabled(cJSON_IsTrue(item));
    }
    if ((item = cJSON_GetObjectItem(json, "branch")) && cJSON_IsString(item)
            && strlen(item->valuestring) > 0) {
        config_set_auto_update_branch(item->valuestring);
    }
    bool trigger = false;
    if ((item = cJSON_GetObjectItem(json, "trigger_now")) && cJSON_IsTrue(item)) {
        trigger = true;
    }

    cJSON_Delete(json);
    config_save();

    if (trigger) {
        auto_updater_trigger_now();
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", trigger ? "Config saved, check triggered" : "Config saved");
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// ============================================================================
// OPTIONS handlers — CORS preflight (no auth check)
// ============================================================================

esp_err_t api_ota_status_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t api_ota_fw_upload_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t api_ota_fw_url_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t api_ota_www_upload_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t api_ota_rollback_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t api_ota_auto_update_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}
