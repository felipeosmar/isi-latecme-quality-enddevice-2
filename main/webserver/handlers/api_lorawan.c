/**
 * @file api_lorawan.c
 * @brief LoRaWAN API handlers
 *
 * GET  /api/lorawan/status - LoRaWAN join status and statistics
 * GET  /api/lorawan/config - LoRaWAN configuration
 * POST /api/lorawan/config - Update LoRaWAN configuration
 * POST /api/lorawan/join   - Force re-join
 */

#include "handlers.h"
#include "lorawan_handler.h"

static const char *TAG = "API_LORAWAN";

// GET /api/lorawan/status
esp_err_t api_lorawan_status_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    lorawan_stats_t stats;
    lorawan_get_stats(&stats);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "joined", stats.joined);
    cJSON_AddNumberToObject(root, "dev_addr", stats.dev_addr);
    cJSON_AddNumberToObject(root, "uplink_count", stats.uplink_count);
    cJSON_AddNumberToObject(root, "downlink_count", stats.downlink_count);
    cJSON_AddNumberToObject(root, "last_rssi", stats.last_rssi);
    cJSON_AddNumberToObject(root, "last_snr", stats.last_snr);
    cJSON_AddNumberToObject(root, "data_rate", stats.data_rate);
    cJSON_AddNumberToObject(root, "last_uplink_ms", stats.last_uplink_ms);
    cJSON_AddNumberToObject(root, "join_attempts", stats.join_attempts);

    char dev_addr_hex[9];
    snprintf(dev_addr_hex, sizeof(dev_addr_hex), "%08lX", (unsigned long)stats.dev_addr);
    cJSON_AddStringToObject(root, "dev_addr_hex", dev_addr_hex);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// OPTIONS /api/lorawan/status
esp_err_t api_lorawan_status_options_handler(httpd_req_t *req)
{
    // NO check_auth — browsers never send credentials in preflight
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// GET /api/lorawan/config
esp_err_t api_lorawan_config_get_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "dev_eui", config_get_dev_eui());
    cJSON_AddStringToObject(root, "join_eui", config_get_join_eui());
    cJSON_AddStringToObject(root, "app_key", config_get_app_key());
    cJSON_AddNumberToObject(root, "port", config_get_lorawan_port());
    cJSON_AddNumberToObject(root, "uplink_interval", config_get_uplink_interval());
    cJSON_AddNumberToObject(root, "sub_band", config_get_sub_band());
    cJSON_AddBoolToObject(root, "adr_enabled", config_get_adr_enabled());

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// POST /api/lorawan/config
esp_err_t api_lorawan_config_post_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    char buf[512];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *item;
    if ((item = cJSON_GetObjectItem(json, "dev_eui")) && cJSON_IsString(item)) {
        config_set_dev_eui(item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(json, "join_eui")) && cJSON_IsString(item)) {
        config_set_join_eui(item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(json, "app_key")) && cJSON_IsString(item)) {
        config_set_app_key(item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(json, "port")) && cJSON_IsNumber(item)) {
        config_set_lorawan_port((uint8_t)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "uplink_interval")) && cJSON_IsNumber(item)) {
        config_set_uplink_interval((uint32_t)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "sub_band")) && cJSON_IsNumber(item)) {
        config_set_sub_band((uint8_t)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "adr_enabled")) && cJSON_IsBool(item)) {
        config_set_adr_enabled(cJSON_IsTrue(item));
    }

    cJSON_Delete(json);

    // Save config
    config_save();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", "LoRaWAN config saved. Restart to apply.");

    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// OPTIONS /api/lorawan/config
esp_err_t api_lorawan_config_options_handler(httpd_req_t *req)
{
    // NO check_auth — browsers never send credentials in preflight
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// POST /api/lorawan/join
esp_err_t api_lorawan_join_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    cJSON *resp = cJSON_CreateObject();

    esp_err_t ret = lorawan_force_rejoin();
    if (ret == ESP_OK) {
        cJSON_AddBoolToObject(resp, "success", true);
        cJSON_AddStringToObject(resp, "message", "Join successful");
    } else {
        cJSON_AddBoolToObject(resp, "success", false);
        cJSON_AddStringToObject(resp, "message", "Join failed");
    }

    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}
