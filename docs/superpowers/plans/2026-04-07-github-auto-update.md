# GitHub Auto-Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** O dispositivo verifica diariamente no GitHub se há uma nova release e aplica OTA automaticamente (firmware + www) quando conectado em WiFi STA.

**Architecture:** Uma nova task FreeRTOS (`auto_updater_task`) dorme 24h, acorda, verifica WiFi STA, consulta a GitHub Releases API, compara o número sequencial N do tag com o N armazenado em config, e faz OTA do firmware e/ou www conforme necessário. Firmware e www são rastreados por tags separados para sobreviver a reboots entre as duas atualizações.

**Tech Stack:** ESP-IDF 5.5.3, FreeRTOS, `esp_http_client`, `esp_ota_ops`, `esp_partition`, `cJSON`, `esp_crt_bundle` (HTTPS).

---

## File Map

| Arquivo | Ação |
|---|---|
| `main/update/auto_updater.h` | Criar |
| `main/update/auto_updater.c` | Criar |
| `main/CMakeLists.txt` | Modificar — adicionar fonte + include dir + esp_crt_bundle |
| `main/config/config_manager.c` | Modificar — 4 novos campos + getters/setters + JSON |
| `main/config/config_manager.h` | Modificar — declarar getters/setters |
| `main/webserver/handlers/api_ota.c` | Modificar — 2 novos handlers |
| `main/webserver/handlers/handlers.h` | Modificar — declarar novos handlers |
| `main/webserver/web_server.c` | Modificar — registrar 2 novas rotas |
| `main/main.c` | Modificar — incluir header + chamar `auto_updater_init()` |

---

## Task 1: Adicionar campos de config para auto-update

**Files:**
- Modify: `main/config/config_manager.c`
- Modify: `main/config/config_manager.h`

### Passo a passo

- [ ] **1.1 — Adicionar campos à struct `config_t`**

Em `main/config/config_manager.c`, na `config_t` struct, após o bloco de alarm thresholds (linha ~74), adicionar:

```c
    // Auto-update
    bool  auto_update_enabled;
    char  auto_update_branch[32];
    char  auto_update_firmware_tag[32];
    char  auto_update_www_tag[32];
```

- [ ] **1.2 — Adicionar defaults em `config_reset_defaults()`**

Após o bloco de alarm defaults (linha ~177):

```c
    // Auto-update defaults
    s_config.auto_update_enabled = false;
    strcpy(s_config.auto_update_branch, "main");
    s_config.auto_update_firmware_tag[0] = '\0';
    s_config.auto_update_www_tag[0] = '\0';
```

- [ ] **1.3 — Adicionar serialização em `_config_save_internal()`**

Após o bloco `web` (antes de `char *json_str = cJSON_Print(root);`):

```c
    // Auto-update section
    cJSON *update = cJSON_CreateObject();
    cJSON_AddBoolToObject(update, "enabled", s_config.auto_update_enabled);
    cJSON_AddStringToObject(update, "branch", s_config.auto_update_branch);
    cJSON_AddStringToObject(update, "firmware_tag", s_config.auto_update_firmware_tag);
    cJSON_AddStringToObject(update, "www_tag", s_config.auto_update_www_tag);
    cJSON_AddItemToObject(root, "auto_update", update);
```

- [ ] **1.4 — Adicionar parsing em `config_load()`**

Após o bloco de parsing `web` (antes de `cJSON_Delete(root);`):

```c
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
```

- [ ] **1.5 — Adicionar getters e setters em `config_manager.c`**

No final do arquivo, antes do `#endif`:

```c
// ============================================================================
// Auto-Update Configuration
// ============================================================================

bool config_get_auto_update_enabled(void) {
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(100));
    bool v = s_config.auto_update_enabled;
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
    return v;
}

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
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(100));
    s_config.auto_update_enabled = enabled;
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
}

void config_set_auto_update_branch(const char *branch) {
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(100));
    strncpy(s_config.auto_update_branch, branch, sizeof(s_config.auto_update_branch) - 1);
    s_config.auto_update_branch[sizeof(s_config.auto_update_branch) - 1] = '\0';
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
}

void config_set_auto_update_firmware_tag(const char *tag) {
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(100));
    strncpy(s_config.auto_update_firmware_tag, tag, sizeof(s_config.auto_update_firmware_tag) - 1);
    s_config.auto_update_firmware_tag[sizeof(s_config.auto_update_firmware_tag) - 1] = '\0';
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
}

void config_set_auto_update_www_tag(const char *tag) {
    if (s_config_mutex) xSemaphoreTake(s_config_mutex, pdMS_TO_TICKS(100));
    strncpy(s_config.auto_update_www_tag, tag, sizeof(s_config.auto_update_www_tag) - 1);
    s_config.auto_update_www_tag[sizeof(s_config.auto_update_www_tag) - 1] = '\0';
    if (s_config_mutex) xSemaphoreGive(s_config_mutex);
}
```

- [ ] **1.6 — Declarar getters/setters em `config_manager.h`**

No final de `config_manager.h`, antes do `#ifdef __cplusplus` de fechamento:

```c
// ============================================================================
// Auto-Update Configuration
// ============================================================================

bool        config_get_auto_update_enabled(void);
const char* config_get_auto_update_branch(void);
const char* config_get_auto_update_firmware_tag(void);
const char* config_get_auto_update_www_tag(void);

void config_set_auto_update_enabled(bool enabled);
void config_set_auto_update_branch(const char *branch);
void config_set_auto_update_firmware_tag(const char *tag);
void config_set_auto_update_www_tag(const char *tag);
```

- [ ] **1.7 — Commit**

```bash
git add main/config/config_manager.c main/config/config_manager.h
git commit -m "feat(config): add auto-update config fields (enabled, branch, firmware_tag, www_tag)"
```

---

## Task 2: Criar o módulo `auto_updater`

**Files:**
- Create: `main/update/auto_updater.h`
- Create: `main/update/auto_updater.c`

### Passo a passo

- [ ] **2.1 — Criar o diretório**

```bash
mkdir -p main/update
```

- [ ] **2.2 — Criar `main/update/auto_updater.h`**

```c
/**
 * @file auto_updater.h
 * @brief GitHub automatic OTA update checker
 *
 * Checks once per day for a new GitHub release on the configured branch.
 * If a newer release is found (higher sequential N in tag `{branch}-rN`),
 * downloads and flashes firmware first, then www on the next cycle.
 * Only runs when WiFi is connected in STA mode.
 */

#ifndef AUTO_UPDATER_H
#define AUTO_UPDATER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUTO_UPDATE_RESULT_NEVER,       // Never checked
    AUTO_UPDATE_RESULT_UP_TO_DATE,  // Check ran, nothing to do
    AUTO_UPDATE_RESULT_UPDATED,     // OTA applied, rebooting
    AUTO_UPDATE_RESULT_ERROR,       // Check or OTA failed
} auto_update_result_t;

/**
 * @brief Initialize and start the auto-updater background task.
 *        Call once after WiFi init and web server init.
 * @return ESP_OK on success
 */
esp_err_t auto_updater_init(void);

/**
 * @brief Get the result of the last update check.
 */
auto_update_result_t auto_updater_get_last_result(void);

/**
 * @brief Get Unix timestamp of last check (0 if never checked).
 */
int64_t auto_updater_get_last_check_time(void);

/**
 * @brief Force an immediate check (non-blocking, signals the task).
 *        No-op if a check is already in progress.
 */
void auto_updater_trigger_now(void);

#ifdef __cplusplus
}
#endif

#endif // AUTO_UPDATER_H
```

- [ ] **2.3 — Criar `main/update/auto_updater.c`**

```c
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
#define GITHUB_API_URL      "https://api.github.com/repos/" GITHUB_OWNER "/" GITHUB_REPO "/releases?per_page=30"
#define GITHUB_ASSET_BASE   "https://github.com/" GITHUB_OWNER "/" GITHUB_REPO "/releases/download"

// JSON response buffer size: 30 releases × ~600 bytes each ≈ 18KB
#define API_RESPONSE_BUF_SIZE  20480
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
    ESP_LOGI(TAG, "Fetched %d bytes from GitHub API", buf_pos);

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (!http_ok) { free(buf); return NULL; }
    return buf;
}

/**
 * Find the latest release tag for the given branch from the parsed JSON array.
 * Writes result into `out_tag` (size >= 32). Returns true if found.
 */
static bool find_latest_tag(const char *json, const char *branch,
                             char *out_tag, size_t out_tag_size)
{
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "Failed to parse releases JSON");
        cJSON_Delete(root);
        return false;
    }

    int best_n = -1;
    const char *best_tag = NULL;

    cJSON *release;
    cJSON_ArrayForEach(release, root) {
        cJSON *tag_item = cJSON_GetObjectItem(release, "tag_name");
        if (!tag_item || !cJSON_IsString(tag_item)) continue;

        int n = parse_release_number(tag_item->valuestring, branch);
        if (n > best_n) {
            best_n = n;
            best_tag = tag_item->valuestring;
        }
    }

    if (best_n < 0 || !best_tag) {
        ESP_LOGW(TAG, "No releases found for branch '%s'", branch);
        cJSON_Delete(root);
        return false;
    }

    strncpy(out_tag, best_tag, out_tag_size - 1);
    out_tag[out_tag_size - 1] = '\0';
    ESP_LOGI(TAG, "Latest tag for branch '%s': %s (N=%d)", branch, out_tag, best_n);

    cJSON_Delete(root);
    return true;
}

/**
 * Download firmware from URL and flash via OTA.
 * Returns true on success (reboots internally after success).
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

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
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
    if (err != ESP_OK) { ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err)); return false; }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) { ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err)); return false; }

    ESP_LOGI(TAG, "Firmware flash complete (%lu bytes) → %s", written, update_part->label);
    return true;
}

/**
 * Download www binary from URL and write directly to www partition.
 * Returns true on success (reboots internally after success).
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

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
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

    // Unmount www before writing
    esp_vfs_littlefs_unregister("www");

    err = esp_partition_erase_range(www_part, 0, www_part->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "www erase failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    char *buf = malloc(OTA_CHUNK_SIZE);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    bool error = false;
    uint32_t offset = 0;
    while (1) {
        int n = esp_http_client_read(client, buf, OTA_CHUNK_SIZE);
        if (n < 0) { error = true; break; }
        if (n == 0) break;

        // esp_partition_write requires 4-byte aligned size
        int write_size = (n + 3) & ~3;
        if (write_size > n) memset(buf + n, 0xFF, write_size - n);

        err = esp_partition_write(www_part, offset, buf, write_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "www write failed at offset %lu: %s", offset, esp_err_to_name(err));
            error = true;
            break;
        }
        offset += write_size;
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

    const char *branch = config_get_auto_update_branch();
    if (!branch || strlen(branch) == 0) {
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
    const char *fw_tag  = config_get_auto_update_firmware_tag();
    const char *www_tag = config_get_auto_update_www_tag();

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
        ESP_LOGI(TAG, "New firmware available: %s → %s", fw_tag, latest_tag);

        char fw_url[256];
        snprintf(fw_url, sizeof(fw_url),
                 GITHUB_ASSET_BASE "/%s/lorawan-enddevice-%s.bin",
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
        ESP_LOGI(TAG, "www partition outdated: %s → %s", www_tag, latest_tag);

        char www_url[256];
        snprintf(www_url, sizeof(www_url),
                 GITHUB_ASSET_BASE "/%s/www-%s.bin",
                 latest_tag, latest_tag);

        // Save tag BEFORE unmounting LittleFS (www unmount doesn't affect userdata)
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
```

- [ ] **2.4 — Commit**

```bash
git add main/update/
git commit -m "feat(auto_updater): add GitHub release checker and OTA update module"
```

---

## Task 3: Atualizar CMakeLists.txt

**Files:**
- Modify: `main/CMakeLists.txt`

- [ ] **3.1 — Adicionar fonte, include dir e componente**

Em `main/CMakeLists.txt`:

1. Adicionar `"update/auto_updater.c"` na lista SRCS (após `"interface/button_handler.c"`):
```cmake
        "interface/button_handler.c"
        "update/auto_updater.c"
```

2. Adicionar `"update"` na lista INCLUDE_DIRS (após `"alarm"`):
```cmake
        "alarm"
        "update"
```

3. Adicionar `esp_crt_bundle` na lista REQUIRES (após `esp_http_client`):
```cmake
        esp_http_client
        esp_crt_bundle
```

- [ ] **3.2 — Commit**

```bash
git add main/CMakeLists.txt
git commit -m "build: add auto_updater source, include dir, and esp_crt_bundle dependency"
```

---

## Task 4: Adicionar endpoints de API

**Files:**
- Modify: `main/webserver/handlers/api_ota.c`
- Modify: `main/webserver/handlers/handlers.h`
- Modify: `main/webserver/web_server.c`

- [ ] **4.1 — Adicionar include e handlers em `api_ota.c`**

No topo de `api_ota.c`, adicionar o include (junto com os existentes):

```c
#include "auto_updater.h"
```

No final do arquivo (antes do fim), adicionar os dois handlers:

```c
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

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
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
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}
```

- [ ] **4.2 — Declarar handlers em `handlers.h`**

Em `handlers.h`, após a linha `esp_err_t api_ota_rollback_handler(httpd_req_t *req);`:

```c
esp_err_t api_ota_auto_update_get_handler(httpd_req_t *req);
esp_err_t api_ota_auto_update_post_handler(httpd_req_t *req);
```

- [ ] **4.3 — Registrar rotas em `web_server.c`**

Em `web_server.c`, após o bloco de registro das rotas OTA (após `httpd_register_uri_handler(s_server, &ota_rollback);`):

```c
    httpd_uri_t ota_auto_update_get  = { .uri = "/api/ota/auto-update", .method = HTTP_GET,  .handler = api_ota_auto_update_get_handler };
    httpd_uri_t ota_auto_update_post = { .uri = "/api/ota/auto-update", .method = HTTP_POST, .handler = api_ota_auto_update_post_handler };
    httpd_register_uri_handler(s_server, &ota_auto_update_get);
    httpd_register_uri_handler(s_server, &ota_auto_update_post);
```

- [ ] **4.4 — Commit**

```bash
git add main/webserver/handlers/api_ota.c \
        main/webserver/handlers/handlers.h \
        main/webserver/web_server.c
git commit -m "feat(api): add GET/POST /api/ota/auto-update endpoints"
```

---

## Task 5: Conectar ao `main.c`

**Files:**
- Modify: `main/main.c`

- [ ] **5.1 — Adicionar include**

No bloco de includes de `main.c`, após `#include "button_handler.h"`:

```c
#include "auto_updater.h"
```

- [ ] **5.2 — Adicionar log level para o módulo**

No bloco de `esp_log_level_set` em `app_main()`, após a linha do `CLOCK_SYNC`:

```c
    esp_log_level_set("AUTO_UPD", ESP_LOG_INFO);
```

- [ ] **5.3 — Inicializar o módulo após as tasks FreeRTOS**

Em `app_main()`, após a linha `xTaskCreatePinnedToCore(clock_sync_task, ...)`:

```c
    // Auto-updater task - Core 0, Priority 3, Stack 8KB
    if (auto_updater_init() != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start auto-updater");
    }
```

- [ ] **5.4 — Commit**

```bash
git add main/main.c
git commit -m "feat(main): initialize auto-updater module"
```

---

## Task 6: Build de verificação

- [ ] **6.1 — Compilar o projeto**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh
idf.py build
```

Saída esperada: `Project build complete.` sem erros. Avisos menores são aceitáveis.

Erros comuns e soluções:
- `esp_crt_bundle.h: No such file` → verificar se `esp_crt_bundle` está em REQUIRES no CMakeLists.txt
- `auto_updater.h: No such file` → verificar se `"update"` está em INCLUDE_DIRS
- `WIFI_STATUS_CONNECTED undeclared` → verificar `wifi_manager.h` para o nome exato do enum (pode ser `WIFI_STATUS_CONNECTED` ou `WIFI_STATUS_CONNECTED_STA`)

- [ ] **6.2 — Commit final se necessário**

Se foram feitas correções de compilação:

```bash
git add -p
git commit -m "fix(auto_updater): resolve build issues"
```

---

## Task 7: Teste manual no dispositivo

- [ ] **7.1 — Flash e monitorar**

```bash
./flash.sh update
idf.py -p /dev/ttyUSB0 monitor
```

- [ ] **7.2 — Verificar que a task inicia**

No log, buscar:
```
I (xxxx) AUTO_UPD: Auto-updater initialized (branch: main, enabled: no)
I (xxxx) AUTO_UPD: Auto-updater task started
```

- [ ] **7.3 — Habilitar e configurar via API**

```bash
curl -u admin:admin -X POST http://<device-ip>/api/ota/auto-update \
     -H "Content-Type: application/json" \
     -d '{"enabled": true, "branch": "salt_spray"}'
```

Resposta esperada: `{"success":true,"message":"Config saved"}`

- [ ] **7.4 — Forçar verificação imediata**

```bash
curl -u admin:admin -X POST http://<device-ip>/api/ota/auto-update \
     -H "Content-Type: application/json" \
     -d '{"enabled": true, "branch": "salt_spray", "trigger_now": true}'
```

No log, buscar:
```
I (xxxx) AUTO_UPD: GitHub API status=200 content_len=...
I (xxxx) AUTO_UPD: Latest tag for branch 'salt_spray': salt_spray-r4 (N=4)
I (xxxx) AUTO_UPD: First check: storing current latest tag 'salt_spray-r4' without updating
```
(ou "Already up to date" se já havia tag armazenado)

- [ ] **7.5 — Verificar status via GET**

```bash
curl -u admin:admin http://<device-ip>/api/ota/auto-update
```

Resposta esperada:
```json
{
  "enabled": true,
  "branch": "salt_spray",
  "firmware_tag": "salt_spray-r4",
  "www_tag": "salt_spray-r4",
  "last_check_time": 1234567890,
  "last_check_result": "up_to_date"
}
```
