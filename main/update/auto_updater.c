/**
 * @file auto_updater.c
 * @brief GitHub automatic OTA update checker
 */

#include "auto_updater.h"
#include "config_manager.h"
#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_littlefs.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>     // time(NULL) for Unix timestamp

static const char *TAG = "AUTO_UPD";

// GitHub repository (public, no auth needed)
#define GITHUB_OWNER        "felipeosmar"
#define GITHUB_REPO         "isi-latecme-quality-enddevice-2"
// /releases/latest: returns the single release marked as "Latest" on GitHub.
// More reliable than paginated list — GitHub sorts list by created_at, not
// published_at, so per_page=1 can return an older release if the latest was
// re-published after a newer draft was created.
#define GITHUB_API_URL      "https://api.github.com/repos/" GITHUB_OWNER "/" GITHUB_REPO "/releases/latest"
#define GITHUB_ASSET_BASE   "https://github.com/" GITHUB_OWNER "/" GITHUB_REPO "/releases/download"

// Compile-time variant string — selects the correct firmware binary from the release
#ifdef CONFIG_THERMOCOUPLE_ENABLED
#define FIRMWARE_VARIANT    "salt_spray"
#else
#define FIRMWARE_VARIANT    "standard"
#endif

// JSON response buffer size: GitHub releases include auto-generated notes which can be large.
// 32KB provides comfortable margin for 5 releases with long release notes.
#define API_RESPONSE_BUF_SIZE  32768
#define OTA_CHUNK_SIZE         4096

// Task wakeup interval: 24 hours
#define CHECK_INTERVAL_MS  (24ULL * 60 * 60 * 1000)

// Event bit to force immediate check
#define TRIGGER_BIT  BIT0

// ============================================================================
// State
// ============================================================================

static EventGroupHandle_t   s_event_group  = NULL;
static auto_update_result_t s_last_result  = AUTO_UPDATE_RESULT_NEVER;
static int64_t              s_last_check   = 0;   // Unix seconds
static bool                 s_initialized  = false;

// ============================================================================
// Helpers
// ============================================================================

/**
 * Extract the release number N from a tag like "main-r5" or "salt_spray-r12".
 * Returns -1 if the tag doesn't match the expected pattern.
 */
static int parse_release_number(const char *tag, const char *branch)
{
    // Build expected prefix: "{branch}-r"
    char prefix[48];
    snprintf(prefix, sizeof(prefix), "%s-r", branch);
    size_t plen = strlen(prefix);

    if (strncmp(tag, prefix, plen) != 0) return -1;

    const char *num_str = tag + plen;
    if (*num_str == '\0') return -1;

    char *end;
    long n = strtol(num_str, &end, 10);
    if (*end != '\0' || n < 0) return -1;
    return (int)n;
}

/**
 * Fetch the full GitHub releases JSON into a heap-allocated buffer.
 * Caller must free() the returned pointer. Returns NULL on failure.
 */
static char *github_fetch_releases_json(void)
{
    char *buf = malloc(API_RESPONSE_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "OOM allocating API response buffer");
        return NULL;
    }

    int buf_pos = 0;
    bool http_ok = false;

    esp_http_client_config_t cfg = {
        .url                    = GITHUB_API_URL,
        .timeout_ms             = 15000,
        .buffer_size            = 2048,
        .crt_bundle_attach      = esp_crt_bundle_attach,
        .max_redirection_count  = 5,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(buf); return NULL; }

    // Add required headers
    esp_http_client_set_header(client, "User-Agent", "ESP32-AutoUpdater/1.0");
    esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "GitHub API status=%d content_len=%d", status, content_len);

    if (status != 200) {
        ESP_LOGE(TAG, "GitHub API returned HTTP %d", status);
        goto cleanup;
    }

    while (buf_pos < API_RESPONSE_BUF_SIZE - 1) {
        int to_read = API_RESPONSE_BUF_SIZE - 1 - buf_pos;
        int n = esp_http_client_read(client, buf + buf_pos, to_read);
        if (n < 0) { ESP_LOGE(TAG, "HTTP read error"); goto cleanup; }
        if (n == 0) break;
        buf_pos += n;
    }
    buf[buf_pos] = '\0';
    http_ok = true;
    bool truncated = (buf_pos == API_RESPONSE_BUF_SIZE - 1);
    ESP_LOGI(TAG, "Fetched %d bytes from GitHub API%s", buf_pos, truncated ? " (TRUNCATED)" : "");
    if (truncated) {
        ESP_LOGW(TAG, "Response truncated at %d bytes — increase API_RESPONSE_BUF_SIZE", API_RESPONSE_BUF_SIZE);
    }

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (!http_ok) { free(buf); return NULL; }
    return buf;
}

/**
 * Extract the release tag from the /releases/latest JSON object and validate
 * it belongs to the given branch (matches "{branch}-rN" pattern).
 * Writes result into `out_tag` (size >= 32). Returns true if found.
 */
static bool find_latest_tag(const char *json, const char *branch,
                             char *out_tag, size_t out_tag_size)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "cJSON_Parse failed — first 120 chars: %.120s", json);
        return false;
    }
    if (!cJSON_IsObject(root)) {
        ESP_LOGE(TAG, "Expected JSON object, got type %d — first 120 chars: %.120s", root->type, json);
        cJSON_Delete(root);
        return false;
    }

    cJSON *tag_item = cJSON_GetObjectItem(root, "tag_name");
    if (!tag_item || !cJSON_IsString(tag_item)) {
        ESP_LOGE(TAG, "Missing 'tag_name' in release JSON");
        cJSON_Delete(root);
        return false;
    }

    int n = parse_release_number(tag_item->valuestring, branch);
    if (n < 0) {
        ESP_LOGW(TAG, "No releases found for branch '%s' (latest tag is '%s')",
                 branch, tag_item->valuestring);
        cJSON_Delete(root);
        return false;
    }

    strncpy(out_tag, tag_item->valuestring, out_tag_size - 1);
    out_tag[out_tag_size - 1] = '\0';
    ESP_LOGI(TAG, "Latest tag for branch '%s': %s (N=%d)", branch, out_tag, n);

    cJSON_Delete(root);
    return true;
}

/**
 * Download firmware from URL and flash via OTA.
 * Returns true on success (caller handles reboot).
 */
static bool flash_firmware_from_url(const char *url)
{
    ESP_LOGI(TAG, "Flashing firmware from: %s", url);

    esp_http_client_config_t cfg = {
        .url                    = url,
        .timeout_ms             = 60000,
        .buffer_size            = OTA_CHUNK_SIZE,
        .crt_bundle_attach      = esp_crt_bundle_attach,
        .max_redirection_count  = 5,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { ESP_LOGE(TAG, "HTTP client init failed"); return false; }

    esp_http_client_set_header(client, "User-Agent", "ESP32-AutoUpdater/1.0");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    // Follow redirects — GitHub release assets redirect (302) to CDN.
    // esp_http_client streaming API does not follow redirects automatically.
    int content_len = 0;
    int status = 0;
    for (int redir = 0; redir <= 5; redir++) {
        content_len = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status < 300 || status >= 400) break;
        ESP_LOGI(TAG, "Firmware HTTP %d redirect (hop %d)", status, redir + 1);
        esp_http_client_close(client);
        if (esp_http_client_set_redirection(client) != ESP_OK || redir == 5) {
            ESP_LOGE(TAG, "Redirect failed or limit reached");
            esp_http_client_cleanup(client);
            return false;
        }
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP open (redirect) failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return false;
        }
    }
    if (status != 200) {
        ESP_LOGE(TAG, "Firmware URL returned HTTP %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    ESP_LOGI(TAG, "Firmware size: %d bytes", content_len);

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "No OTA partition available");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update_part, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    char *buf = malloc(OTA_CHUNK_SIZE);
    if (!buf) {
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    bool error = false;
    uint32_t written = 0;
    while (1) {
        int n = esp_http_client_read(client, buf, OTA_CHUNK_SIZE);
        if (n < 0) { ESP_LOGE(TAG, "HTTP read error at %lu bytes", written); error = true; break; }
        if (n == 0) break;
        err = esp_ota_write(ota_handle, buf, n);
        if (err != ESP_OK) { ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err)); error = true; break; }
        written += n;
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (error) { esp_ota_abort(ota_handle); return false; }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        esp_ota_abort(ota_handle);
        return false;
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) { ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err)); return false; }

    ESP_LOGI(TAG, "Firmware flash complete (%lu bytes) -> %s", written, update_part->label);
    return true;
}

/**
 * Download www binary from URL and write directly to www partition.
 * Returns true on success (caller handles reboot).
 */
static bool flash_www_from_url(const char *url)
{
    ESP_LOGI(TAG, "Flashing www from: %s", url);

    esp_http_client_config_t cfg = {
        .url                    = url,
        .timeout_ms             = 60000,
        .buffer_size            = OTA_CHUNK_SIZE,
        .crt_bundle_attach      = esp_crt_bundle_attach,
        .max_redirection_count  = 5,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { ESP_LOGE(TAG, "HTTP client init failed"); return false; }

    esp_http_client_set_header(client, "User-Agent", "ESP32-AutoUpdater/1.0");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    // Follow redirects — GitHub release assets redirect (302) to CDN.
    int status = 0;
    for (int redir = 0; redir <= 5; redir++) {
        esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status < 300 || status >= 400) break;
        ESP_LOGI(TAG, "www HTTP %d redirect (hop %d)", status, redir + 1);
        esp_http_client_close(client);
        if (esp_http_client_set_redirection(client) != ESP_OK || redir == 5) {
            ESP_LOGE(TAG, "Redirect failed or limit reached");
            esp_http_client_cleanup(client);
            return false;
        }
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP open (redirect) failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return false;
        }
    }
    if (status != 200) {
        ESP_LOGE(TAG, "www URL returned HTTP %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    const esp_partition_t *www_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    if (!www_part) {
        ESP_LOGE(TAG, "www partition not found");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Check content length if known (may be 0 if server doesn't report it)
    int content_length = esp_http_client_get_content_length(client);
    if (content_length > 0 && (uint32_t)content_length > www_part->size) {
        ESP_LOGE(TAG, "www binary (%d bytes) exceeds partition size (%lu bytes)",
                 content_length, www_part->size);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    // Unmount www before writing
    esp_err_t unregister_err = esp_vfs_littlefs_unregister("www");
    if (unregister_err != ESP_OK && unregister_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "www littlefs unregister: %s", esp_err_to_name(unregister_err));
    }

    err = esp_partition_erase_range(www_part, 0, www_part->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "www erase failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    char *buf = malloc(OTA_CHUNK_SIZE + 3);  // +3 for 4-byte alignment padding on last chunk
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    bool error = false;
    uint32_t offset = 0;
    uint32_t buf_fill = 0;

    while (1) {
        int n = esp_http_client_read(client, buf + buf_fill, OTA_CHUNK_SIZE - (int)buf_fill);
        if (n < 0) { ESP_LOGE(TAG, "www HTTP read error at offset %lu", offset); error = true; break; }
        if (n == 0) break;
        buf_fill += (uint32_t)n;

        if (buf_fill < OTA_CHUNK_SIZE) continue; // Fill buffer completely before writing

        // Full aligned chunk: write directly (OTA_CHUNK_SIZE is 4096, always 4-byte aligned)
        err = esp_partition_write(www_part, offset, buf, OTA_CHUNK_SIZE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "www write failed at offset %lu: %s", offset, esp_err_to_name(err));
            error = true;
            break;
        }
        offset += OTA_CHUNK_SIZE;
        buf_fill = 0;
    }

    // Write remaining bytes (last partial chunk), padded to 4-byte alignment
    if (!error && buf_fill > 0) {
        uint32_t write_size = (buf_fill + 3) & ~3U;
        if (write_size > buf_fill) memset(buf + buf_fill, 0xFF, write_size - buf_fill);
        err = esp_partition_write(www_part, offset, buf, write_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "www write failed at offset %lu: %s", offset, esp_err_to_name(err));
            error = true;
        } else {
            offset += buf_fill;  // Track actual data bytes written
        }
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (error) { return false; }

    ESP_LOGI(TAG, "www flash complete (%lu bytes)", offset);
    return true;
}

// ============================================================================
// Core check logic
// ============================================================================

static void run_check(void)
{
    // Only run in STA mode with active connection
    if (wifi_manager_get_status() != WIFI_STATUS_CONNECTED) {
        ESP_LOGI(TAG, "WiFi not connected in STA mode, skipping check");
        return;
    }

    if (!config_get_auto_update_enabled()) {
        ESP_LOGI(TAG, "Auto-update disabled, skipping check");
        return;
    }

    char branch[32];
    strncpy(branch, config_get_auto_update_branch(), sizeof(branch) - 1);
    branch[sizeof(branch) - 1] = '\0';

    if (strlen(branch) == 0) {
        ESP_LOGW(TAG, "No branch configured");
        s_last_result = AUTO_UPDATE_RESULT_ERROR;
        return;
    }

    s_last_check = (int64_t)time(NULL);  // Unix seconds (0 before clock sync)

    // Fetch releases JSON
    char *json = github_fetch_releases_json();
    if (!json) {
        ESP_LOGE(TAG, "Failed to fetch GitHub releases");
        s_last_result = AUTO_UPDATE_RESULT_ERROR;
        return;
    }

    // Find latest tag for the configured branch
    char latest_tag[32] = {0};
    if (!find_latest_tag(json, branch, latest_tag, sizeof(latest_tag))) {
        free(json);
        s_last_result = AUTO_UPDATE_RESULT_ERROR;
        return;
    }
    free(json);

    int latest_n = parse_release_number(latest_tag, branch);

    // --- Handle first boot (no stored tags yet) ---
    char fw_tag[32];
    char www_tag[32];
    strncpy(fw_tag,  config_get_auto_update_firmware_tag(), sizeof(fw_tag)  - 1);
    strncpy(www_tag, config_get_auto_update_www_tag(),      sizeof(www_tag) - 1);
    fw_tag[sizeof(fw_tag) - 1]   = '\0';
    www_tag[sizeof(www_tag) - 1] = '\0';

    if (fw_tag[0] == '\0') {
        ESP_LOGI(TAG, "First check: storing current latest tag '%s' without updating", latest_tag);
        config_set_auto_update_firmware_tag(latest_tag);
        config_set_auto_update_www_tag(latest_tag);
        config_save();
        s_last_result = AUTO_UPDATE_RESULT_UP_TO_DATE;
        return;
    }

    int fw_n  = parse_release_number(fw_tag,  branch);
    int www_n = parse_release_number(www_tag, branch);

    // --- Step 1: Update firmware if needed ---
    if (latest_n > fw_n) {
        ESP_LOGI(TAG, "New firmware available: %s -> %s", fw_tag, latest_tag);

        char fw_url[256];
        snprintf(fw_url, sizeof(fw_url),
                 GITHUB_ASSET_BASE "/%s/lorawan-enddevice-%s-" FIRMWARE_VARIANT ".bin",
                 latest_tag, latest_tag);

        if (!flash_firmware_from_url(fw_url)) {
            ESP_LOGE(TAG, "Firmware flash failed");
            s_last_result = AUTO_UPDATE_RESULT_ERROR;
            return;
        }

        // Save tag BEFORE reboot so we don't reflash on next boot
        config_set_auto_update_firmware_tag(latest_tag);
        config_save();

        ESP_LOGI(TAG, "Firmware updated to %s, rebooting...", latest_tag);
        s_last_result = AUTO_UPDATE_RESULT_UPDATED;
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return; // unreachable
    }

    // --- Step 2: Update www if firmware is current but www is not ---
    if (latest_n > www_n) {
        ESP_LOGI(TAG, "www partition outdated: %s -> %s", www_tag, latest_tag);

        char www_url[256];
        snprintf(www_url, sizeof(www_url),
                 GITHUB_ASSET_BASE "/%s/www-%s.bin",
                 latest_tag, latest_tag);

        // Save tag before flashing: www unmount doesn't affect userdata partition.
        // Known risk: if power is lost after save but before flash completes,
        // the device reboots with tag marked updated but www partition erased.
        // The updater will then believe www is current — accepted trade-off
        // to avoid writing to a mounted filesystem.
        config_set_auto_update_www_tag(latest_tag);
        config_save();

        if (!flash_www_from_url(www_url)) {
            ESP_LOGE(TAG, "www flash failed");
            // Revert tag on failure
            config_set_auto_update_www_tag(www_tag);
            config_save();
            s_last_result = AUTO_UPDATE_RESULT_ERROR;
            return;
        }

        ESP_LOGI(TAG, "www updated to %s, rebooting...", latest_tag);
        s_last_result = AUTO_UPDATE_RESULT_UPDATED;
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return; // unreachable
    }

    ESP_LOGI(TAG, "Already up to date (%s)", latest_tag);
    s_last_result = AUTO_UPDATE_RESULT_UP_TO_DATE;
}

// ============================================================================
// Task
// ============================================================================

static void auto_updater_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Auto-updater task started");

    // Initial delay: wait 60s after boot before first check
    // (give WiFi time to fully stabilize)
    xEventGroupWaitBits(s_event_group, TRIGGER_BIT,
                        pdTRUE, pdFALSE,
                        pdMS_TO_TICKS(60000));

    while (1) {
        run_check();

        // Wait 24h OR until triggered manually
        xEventGroupWaitBits(s_event_group, TRIGGER_BIT,
                            pdTRUE, pdFALSE,
                            pdMS_TO_TICKS(CHECK_INTERVAL_MS));
    }
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t auto_updater_init(void)
{
    if (s_initialized) return ESP_OK;

    s_event_group = xEventGroupCreate();
    if (!s_event_group) return ESP_ERR_NO_MEM;

    BaseType_t ret = xTaskCreatePinnedToCore(
        auto_updater_task, "auto_upd",
        8192, NULL, 3, NULL, 0);

    if (ret != pdPASS) {
        vEventGroupDelete(s_event_group);
        s_event_group = NULL;
        return ESP_FAIL;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Auto-updater initialized (branch: %s, enabled: %s)",
             config_get_auto_update_branch(),
             config_get_auto_update_enabled() ? "yes" : "no");
    return ESP_OK;
}

auto_update_result_t auto_updater_get_last_result(void)
{
    return s_last_result;
}

int64_t auto_updater_get_last_check_time(void)
{
    return s_last_check;
}

void auto_updater_trigger_now(void)
{
    if (s_event_group) {
        xEventGroupSetBits(s_event_group, TRIGGER_BIT);
    }
}
