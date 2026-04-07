/**
 * @file web_server.c
 * @brief HTTP Web Server - Core initialization and route registration
 *
 * Handlers are split into separate files in handlers/ directory.
 */

#include "web_server.h"
#include "handlers/handlers.h"

static const char *TAG = "WEB_SRV";

// Server handle
static httpd_handle_t s_server = NULL;
static bool s_running = false;

// Shared configuration (accessed by handlers via extern)
web_server_config_t g_web_config;

// ============================================================================
// Default Configuration
// ============================================================================

void web_server_get_default_config(web_server_config_t *config)
{
    if (config == NULL) return;

    config->port = 80;
    config->username = "admin";
    config->password = "admin";
    config->auth_enabled = false;
}

// ============================================================================
// Server Initialization
// ============================================================================

esp_err_t web_server_init(const web_server_config_t *config)
{
    if (s_running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }

    // Initialize LittleFS for www partition (web interface files)
    esp_vfs_littlefs_conf_t www_conf = {
        .base_path = "/www",
        .partition_label = "www",
        .format_if_mount_failed = false,
        .dont_mount = false
    };

    esp_err_t ret = esp_vfs_littlefs_register(&www_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount www partition: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info("www", &total, &used);
    ESP_LOGI(TAG, "LittleFS www: total=%d, used=%d", total, used);

    // Store configuration
    if (config) {
        memcpy(&g_web_config, config, sizeof(web_server_config_t));
    } else {
        web_server_get_default_config(&g_web_config);
    }

    // Configure HTTP server
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = g_web_config.port;
    http_config.max_uri_handlers = 50;
    http_config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting server on port %d", http_config.server_port);

    ret = httpd_start(&s_server, &http_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }

    // ========================================================================
    // Register Routes - Static Files
    // ========================================================================

    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
    httpd_uri_t css_uri = { .uri = "/style.css", .method = HTTP_GET, .handler = css_handler };
    httpd_uri_t js_uri = { .uri = "/core.js", .method = HTTP_GET, .handler = js_handler };
    httpd_uri_t favicon_uri = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler };

    httpd_register_uri_handler(s_server, &index_uri);
    httpd_register_uri_handler(s_server, &css_uri);
    httpd_register_uri_handler(s_server, &js_uri);
    httpd_register_uri_handler(s_server, &favicon_uri);

    // Tabs HTML
    httpd_uri_t tabs_sensors_html = { .uri = "/tabs/sensors.html", .method = HTTP_GET, .handler = tabs_html_handler };
    httpd_uri_t tabs_lorawan_html = { .uri = "/tabs/lorawan.html", .method = HTTP_GET, .handler = tabs_html_handler };
    httpd_uri_t tabs_system_html = { .uri = "/tabs/system.html", .method = HTTP_GET, .handler = tabs_html_handler };
    httpd_uri_t tabs_config_html = { .uri = "/tabs/config.html", .method = HTTP_GET, .handler = tabs_html_handler };
    httpd_uri_t tabs_files_html = { .uri = "/tabs/files.html", .method = HTTP_GET, .handler = tabs_html_handler };
    httpd_uri_t tabs_tasks_html = { .uri = "/tabs/tasks.html", .method = HTTP_GET, .handler = tabs_html_handler };

    httpd_register_uri_handler(s_server, &tabs_sensors_html);
    httpd_register_uri_handler(s_server, &tabs_lorawan_html);
    httpd_register_uri_handler(s_server, &tabs_system_html);
    httpd_register_uri_handler(s_server, &tabs_config_html);
    httpd_register_uri_handler(s_server, &tabs_files_html);
    httpd_register_uri_handler(s_server, &tabs_tasks_html);

    // Tabs JS
    httpd_uri_t tabs_sensors_js = { .uri = "/tabs/sensors.js", .method = HTTP_GET, .handler = tabs_js_handler };
    httpd_uri_t tabs_lorawan_js = { .uri = "/tabs/lorawan.js", .method = HTTP_GET, .handler = tabs_js_handler };
    httpd_uri_t tabs_system_js = { .uri = "/tabs/system.js", .method = HTTP_GET, .handler = tabs_js_handler };
    httpd_uri_t tabs_config_js = { .uri = "/tabs/config.js", .method = HTTP_GET, .handler = tabs_js_handler };
    httpd_uri_t tabs_files_js = { .uri = "/tabs/files.js", .method = HTTP_GET, .handler = tabs_js_handler };
    httpd_uri_t tabs_tasks_js = { .uri = "/tabs/tasks.js", .method = HTTP_GET, .handler = tabs_js_handler };

    httpd_register_uri_handler(s_server, &tabs_sensors_js);
    httpd_register_uri_handler(s_server, &tabs_lorawan_js);
    httpd_register_uri_handler(s_server, &tabs_system_js);
    httpd_register_uri_handler(s_server, &tabs_config_js);
    httpd_register_uri_handler(s_server, &tabs_files_js);
    httpd_register_uri_handler(s_server, &tabs_tasks_js);

    httpd_uri_t tabs_ota_html = { .uri = "/tabs/ota.html", .method = HTTP_GET, .handler = tabs_html_handler };
    httpd_uri_t tabs_ota_js = { .uri = "/tabs/ota.js", .method = HTTP_GET, .handler = tabs_js_handler };
    httpd_register_uri_handler(s_server, &tabs_ota_html);
    httpd_register_uri_handler(s_server, &tabs_ota_js);

    // ========================================================================
    // Register Routes - API: File Manager
    // ========================================================================

    httpd_uri_t files_list = { .uri = "/api/files/list", .method = HTTP_GET, .handler = api_files_list_handler };
    httpd_uri_t files_info = { .uri = "/api/files/info", .method = HTTP_GET, .handler = api_files_info_handler };
    httpd_uri_t files_download = { .uri = "/api/files/download", .method = HTTP_GET, .handler = api_files_download_handler };
    httpd_uri_t files_view = { .uri = "/api/files/view", .method = HTTP_GET, .handler = api_files_view_handler };
    httpd_uri_t files_read = { .uri = "/api/files/read", .method = HTTP_GET, .handler = api_files_read_handler };
    httpd_uri_t files_write = { .uri = "/api/files/write", .method = HTTP_POST, .handler = api_files_write_handler };
    httpd_uri_t files_delete = { .uri = "/api/files/delete", .method = HTTP_POST, .handler = api_files_delete_handler };
    httpd_uri_t files_mkdir = { .uri = "/api/files/mkdir", .method = HTTP_POST, .handler = api_files_mkdir_handler };
    httpd_uri_t files_upload = { .uri = "/api/files/upload", .method = HTTP_POST, .handler = api_files_upload_handler };

    httpd_register_uri_handler(s_server, &files_list);
    httpd_register_uri_handler(s_server, &files_info);
    httpd_register_uri_handler(s_server, &files_download);
    httpd_register_uri_handler(s_server, &files_view);
    httpd_register_uri_handler(s_server, &files_read);
    httpd_register_uri_handler(s_server, &files_write);
    httpd_register_uri_handler(s_server, &files_delete);
    httpd_register_uri_handler(s_server, &files_mkdir);
    httpd_register_uri_handler(s_server, &files_upload);

    // ========================================================================
    // Register Routes - API: System
    // ========================================================================

    httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler };
    httpd_uri_t tasks = { .uri = "/api/tasks", .method = HTTP_GET, .handler = api_tasks_handler };
    httpd_uri_t restart = { .uri = "/api/restart", .method = HTTP_POST, .handler = api_restart_handler };
    httpd_uri_t restart_options = { .uri = "/api/restart", .method = HTTP_OPTIONS, .handler = api_restart_options_handler };
    httpd_uri_t logs = { .uri = "/api/logs", .method = HTTP_GET, .handler = api_logs_handler };
    httpd_uri_t logs_clear = { .uri = "/api/logs/clear", .method = HTTP_POST, .handler = api_logs_clear_handler };

    httpd_register_uri_handler(s_server, &status);
    httpd_register_uri_handler(s_server, &tasks);
    httpd_register_uri_handler(s_server, &restart);
    httpd_register_uri_handler(s_server, &restart_options);
    httpd_register_uri_handler(s_server, &logs);
    httpd_register_uri_handler(s_server, &logs_clear);

    // ========================================================================
    // Register Routes - API: WiFi
    // ========================================================================

    httpd_uri_t wifi_scan = { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = api_wifi_scan_handler };
    httpd_uri_t wifi_connect = { .uri = "/api/wifi/connect", .method = HTTP_POST, .handler = api_wifi_connect_handler };
    httpd_uri_t wifi_status = { .uri = "/api/wifi/status", .method = HTTP_GET, .handler = api_wifi_status_handler };

    httpd_register_uri_handler(s_server, &wifi_scan);
    httpd_register_uri_handler(s_server, &wifi_connect);
    httpd_register_uri_handler(s_server, &wifi_status);

    // ========================================================================
    // Register Routes - API: Sensors
    // ========================================================================

    httpd_uri_t sensors_status = { .uri = "/api/sensors/status", .method = HTTP_GET, .handler = api_sensors_status_handler };
    httpd_uri_t sensors_status_options = { .uri = "/api/sensors/status", .method = HTTP_OPTIONS, .handler = api_sensors_status_options_handler };
    httpd_uri_t sensors_config_get = { .uri = "/api/sensors/config", .method = HTTP_GET, .handler = api_sensors_config_get_handler };
    httpd_uri_t sensors_config_post = { .uri = "/api/sensors/config", .method = HTTP_POST, .handler = api_sensors_config_post_handler };

    httpd_register_uri_handler(s_server, &sensors_status);
    httpd_register_uri_handler(s_server, &sensors_status_options);
    httpd_register_uri_handler(s_server, &sensors_config_get);
    httpd_register_uri_handler(s_server, &sensors_config_post);

    // ========================================================================
    // Register Routes - API: LoRaWAN
    // ========================================================================

    httpd_uri_t lorawan_status = { .uri = "/api/lorawan/status", .method = HTTP_GET, .handler = api_lorawan_status_handler };
    httpd_uri_t lorawan_status_options = { .uri = "/api/lorawan/status", .method = HTTP_OPTIONS, .handler = api_lorawan_status_options_handler };
    httpd_uri_t lorawan_config_get = { .uri = "/api/lorawan/config", .method = HTTP_GET, .handler = api_lorawan_config_get_handler };
    httpd_uri_t lorawan_config_post = { .uri = "/api/lorawan/config", .method = HTTP_POST, .handler = api_lorawan_config_post_handler };
    httpd_uri_t lorawan_config_options = { .uri = "/api/lorawan/config", .method = HTTP_OPTIONS, .handler = api_lorawan_config_options_handler };
    httpd_uri_t lorawan_join = { .uri = "/api/lorawan/join", .method = HTTP_POST, .handler = api_lorawan_join_handler };

    httpd_register_uri_handler(s_server, &lorawan_status);
    httpd_register_uri_handler(s_server, &lorawan_status_options);
    httpd_register_uri_handler(s_server, &lorawan_config_get);
    httpd_register_uri_handler(s_server, &lorawan_config_post);
    httpd_register_uri_handler(s_server, &lorawan_config_options);
    httpd_register_uri_handler(s_server, &lorawan_join);

    // ========================================================================
    // Register Routes - API: OTA
    // ========================================================================

    httpd_uri_t ota_status = { .uri = "/api/ota/status", .method = HTTP_GET, .handler = api_ota_status_handler };
    httpd_uri_t ota_fw_upload = { .uri = "/api/ota/firmware/upload", .method = HTTP_POST, .handler = api_ota_firmware_upload_handler };
    httpd_uri_t ota_fw_url = { .uri = "/api/ota/firmware/url", .method = HTTP_POST, .handler = api_ota_firmware_url_handler };
    httpd_uri_t ota_www_upload = { .uri = "/api/ota/www/upload", .method = HTTP_POST, .handler = api_ota_www_upload_handler };
    httpd_uri_t ota_rollback = { .uri = "/api/ota/rollback", .method = HTTP_POST, .handler = api_ota_rollback_handler };

    httpd_register_uri_handler(s_server, &ota_status);
    httpd_register_uri_handler(s_server, &ota_fw_upload);
    httpd_register_uri_handler(s_server, &ota_fw_url);
    httpd_register_uri_handler(s_server, &ota_www_upload);
    httpd_register_uri_handler(s_server, &ota_rollback);

    // ========================================================================
    // Initialization Complete
    // ========================================================================

    s_running = true;
    ESP_LOGI(TAG, "Web server started");

    return ESP_OK;
}

// ============================================================================
// Server Control
// ============================================================================

void web_server_deinit(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    esp_vfs_littlefs_unregister("www");
    s_running = false;
}

bool web_server_is_running(void)
{
    return s_running;
}

esp_err_t web_server_set_auth(const char *username, const char *password)
{
    if (username) g_web_config.username = username;
    if (password) g_web_config.password = password;
    return ESP_OK;
}
