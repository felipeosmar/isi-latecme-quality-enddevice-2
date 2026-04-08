/**
 * @file handlers.h
 * @brief Common definitions for all HTTP request handlers
 */

#ifndef HANDLERS_H
#define HANDLERS_H

#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_manager.h"
#include "config_manager.h"
#include "log_buffer.h"
#include "web_server.h"
#include "sensor_manager.h"
#include "lorawan_handler.h"

// ============================================================================
// Shared state (from web_server.c)
// ============================================================================

// Server configuration (set during init)
extern web_server_config_t g_web_config;

// ============================================================================
// Authentication (auth.c)
// ============================================================================

/**
 * @brief Check Basic Auth credentials
 * @param req HTTP request
 * @return true if authenticated or auth disabled
 */
bool check_auth(httpd_req_t *req);

/**
 * @brief Send 401 Unauthorized response
 */
esp_err_t send_unauthorized(httpd_req_t *req);

// ============================================================================
// Static file handlers (static_files.c)
// ============================================================================

esp_err_t serve_file(httpd_req_t *req, const char *filepath, const char *content_type);
esp_err_t index_handler(httpd_req_t *req);
esp_err_t css_handler(httpd_req_t *req);
esp_err_t js_handler(httpd_req_t *req);
esp_err_t tabs_html_handler(httpd_req_t *req);
esp_err_t tabs_js_handler(httpd_req_t *req);
esp_err_t favicon_handler(httpd_req_t *req);

// ============================================================================
// API - Files (api_files.c)
// ============================================================================

esp_err_t api_files_list_handler(httpd_req_t *req);
esp_err_t api_files_info_handler(httpd_req_t *req);
esp_err_t api_files_download_handler(httpd_req_t *req);
esp_err_t api_files_view_handler(httpd_req_t *req);
esp_err_t api_files_read_handler(httpd_req_t *req);
esp_err_t api_files_write_handler(httpd_req_t *req);
esp_err_t api_files_delete_handler(httpd_req_t *req);
esp_err_t api_files_mkdir_handler(httpd_req_t *req);
esp_err_t api_files_upload_handler(httpd_req_t *req);

// ============================================================================
// API - System (api_system.c)
// ============================================================================

esp_err_t api_status_handler(httpd_req_t *req);
esp_err_t api_tasks_handler(httpd_req_t *req);
esp_err_t api_restart_handler(httpd_req_t *req);
esp_err_t api_restart_options_handler(httpd_req_t *req);
esp_err_t api_logs_handler(httpd_req_t *req);
esp_err_t api_logs_clear_handler(httpd_req_t *req);

// ============================================================================
// API - WiFi (api_wifi.c)
// ============================================================================

esp_err_t api_wifi_scan_handler(httpd_req_t *req);
esp_err_t api_wifi_connect_handler(httpd_req_t *req);
esp_err_t api_wifi_status_handler(httpd_req_t *req);

// ============================================================================
// API - Sensors (api_sensors.c)
// ============================================================================

esp_err_t api_sensors_status_handler(httpd_req_t *req);
esp_err_t api_sensors_status_options_handler(httpd_req_t *req);
esp_err_t api_sensors_config_get_handler(httpd_req_t *req);
esp_err_t api_sensors_config_post_handler(httpd_req_t *req);
esp_err_t api_sensors_config_options_handler(httpd_req_t *req);

// ============================================================================
// API - LoRaWAN (api_lorawan.c)
// ============================================================================

esp_err_t api_lorawan_status_handler(httpd_req_t *req);
esp_err_t api_lorawan_status_options_handler(httpd_req_t *req);
esp_err_t api_lorawan_config_get_handler(httpd_req_t *req);
esp_err_t api_lorawan_config_post_handler(httpd_req_t *req);
esp_err_t api_lorawan_config_options_handler(httpd_req_t *req);
esp_err_t api_lorawan_join_handler(httpd_req_t *req);

// ============================================================================
// API - OTA (api_ota.c)
// ============================================================================

esp_err_t api_ota_status_handler(httpd_req_t *req);
esp_err_t api_ota_status_options_handler(httpd_req_t *req);
esp_err_t api_ota_firmware_upload_handler(httpd_req_t *req);
esp_err_t api_ota_fw_upload_options_handler(httpd_req_t *req);
esp_err_t api_ota_firmware_url_handler(httpd_req_t *req);
esp_err_t api_ota_fw_url_options_handler(httpd_req_t *req);
esp_err_t api_ota_www_upload_handler(httpd_req_t *req);
esp_err_t api_ota_www_upload_options_handler(httpd_req_t *req);
esp_err_t api_ota_rollback_handler(httpd_req_t *req);
esp_err_t api_ota_rollback_options_handler(httpd_req_t *req);
esp_err_t api_ota_auto_update_get_handler(httpd_req_t *req);
esp_err_t api_ota_auto_update_post_handler(httpd_req_t *req);
esp_err_t api_ota_auto_update_options_handler(httpd_req_t *req);

#endif // HANDLERS_H
