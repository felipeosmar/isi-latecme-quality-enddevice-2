/**
 * @file api_sensors.c
 * @brief Sensor API handlers
 *
 * GET  /api/sensors/status - Current sensor readings
 * GET  /api/sensors/config - Sensor configuration
 * POST /api/sensors/config - Update sensor configuration
 */

#include "handlers.h"
#include "sensor_manager.h"
#include "buzzer.h"
#include "alarm_sync.h"
#include "alarm_events.h"
#include <math.h>

static const char *TAG = "API_SENSORS";

// GET /api/sensors/status
esp_err_t api_sensors_status_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    sensor_data_t data;
    sensor_manager_get_data(&data);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "sensor_name", data.sensor_name);
    cJSON_AddBoolToObject(root, "temp_hum_valid", data.temp_hum_valid);
    cJSON_AddNumberToObject(root, "temperature", data.temperature);
    cJSON_AddNumberToObject(root, "humidity", data.humidity);
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    cJSON_AddBoolToObject(root, "thermocouple_hw_enabled", true);
    cJSON_AddBoolToObject(root, "thermocouple_valid", data.thermocouple_valid);
    cJSON_AddNumberToObject(root, "thermocouple_temp", data.thermocouple_temp);
#else
    cJSON_AddBoolToObject(root, "thermocouple_hw_enabled", false);
#endif
    cJSON_AddNumberToObject(root, "timestamp_ms", data.timestamp_ms);

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// OPTIONS /api/sensors/status
esp_err_t api_sensors_status_options_handler(httpd_req_t *req)
{
    // NO check_auth — browsers never send credentials in preflight
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// OPTIONS /api/sensors/config
esp_err_t api_sensors_config_options_handler(httpd_req_t *req)
{
    // NO check_auth — browsers never send credentials in preflight
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Authorization, Content-Type");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// GET /api/sensors/config
esp_err_t api_sensors_config_get_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "interval", config_get_sensor_interval());
    cJSON_AddNumberToObject(root, "temp_correction", config_get_temp_correction());
    cJSON_AddNumberToObject(root, "hum_correction", config_get_hum_correction());
    cJSON_AddStringToObject(root, "device_name", config_get_device_name());
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    cJSON_AddBoolToObject(root, "thermocouple_hw_enabled", true);
    cJSON_AddBoolToObject(root, "thermocouple_enabled", config_get_thermocouple_enabled());
    cJSON_AddNumberToObject(root, "thermocouple_max_temp", config_get_thermocouple_max_temp());
    cJSON_AddNumberToObject(root, "thermocouple_sck_pin", config_get_thermocouple_sck_pin());
    cJSON_AddNumberToObject(root, "thermocouple_so_pin", config_get_thermocouple_so_pin());
    cJSON_AddNumberToObject(root, "thermocouple_cs_pin", config_get_thermocouple_cs_pin());
    cJSON_AddNumberToObject(root, "thermocouple_min_temp", config_get_thermocouple_min_temp());
    cJSON_AddNumberToObject(root, "thermocouple_correction", config_get_thermocouple_correction());
#else
    cJSON_AddBoolToObject(root, "thermocouple_hw_enabled", false);
#endif
    cJSON_AddNumberToObject(root, "buzzer_volume", config_get_buzzer_volume());
    cJSON_AddBoolToObject(root, "alarm_temp_enabled", config_get_alarm_temp_enabled());
    cJSON_AddNumberToObject(root, "alarm_temp_low",   config_get_alarm_temp_low());
    cJSON_AddNumberToObject(root, "alarm_temp_high",  config_get_alarm_temp_high());
    cJSON_AddBoolToObject(root, "alarm_hum_enabled",  config_get_alarm_hum_enabled());
    cJSON_AddNumberToObject(root, "alarm_hum_low",    config_get_alarm_hum_low());
    cJSON_AddNumberToObject(root, "alarm_hum_high",   config_get_alarm_hum_high());
    cJSON_AddNumberToObject(root, "alarm_limits_version", config_get_alarm_limits_version());
    cJSON_AddBoolToObject(root, "alarm_limits_origin_local", config_get_alarm_limits_origin_local());
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    cJSON_AddBoolToObject(root, "alarm_tc_enabled",   config_get_alarm_tc_enabled());
    cJSON_AddNumberToObject(root, "alarm_tc_low",     config_get_alarm_tc_low());
    cJSON_AddNumberToObject(root, "alarm_tc_high",    config_get_alarm_tc_high());
#endif

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
}

// Send a JSON {"success":false,"message":...} with the given HTTP status line.
static esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *msg)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", false);
    cJSON_AddStringToObject(resp, "message", msg);
    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_send(req, json_str ? json_str : "{\"success\":false}", HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}

// float -> tenths, round half away from zero. false if outside int16.
static bool to_tenths(double v, int16_t *out)
{
    double t = round(v * 10.0);
    if (!(t >= -32768.0 && t <= 32767.0)) return false;   // also rejects NaN/inf
    *out = (int16_t)t;
    return true;
}

static const char *msg_for_alarm_err(alarm_sync_err_t e)
{
    return e == ALARM_SYNC_ERR_ORDER ? "M\xC3\xADnimo deve ser menor que o m\xC3\xA1ximo"
                                     : "Fora da faixa f\xC3\xADsica";
}

// POST /api/sensors/config
esp_err_t api_sensors_config_post_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    char buf[1024];
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

    // ---- 1. Validate alarm fields BEFORE touching any setter ----
    static const char *const k_en[3]   = {"alarm_temp_enabled", "alarm_hum_enabled", "alarm_tc_enabled"};
    static const char *const k_low[3]  = {"alarm_temp_low", "alarm_hum_low", "alarm_tc_low"};
    static const char *const k_high[3] = {"alarm_temp_high", "alarm_hum_high", "alarm_tc_high"};

    alarm_channel_cfg_t cur[3];
    config_get_alarm_block(cur);

    alarm_sync_limits_t cand;
    memset(&cand, 0, sizeof(cand));
    bool range_bad = false;
    bool alarm_differs = false;

    for (int c = 0; c < 3; c++) {
        alarm_sync_channel_t *ch = &cand.ch[c];
        bool cur_ok_lo = to_tenths(cur[c].low, &ch->low_d);
        bool cur_ok_hi = to_tenths(cur[c].high, &ch->high_d);
        (void)cur_ok_lo; (void)cur_ok_hi;   // stored values are always in range
        ch->enabled = cur[c].enabled;
        int16_t cur_lo = ch->low_d, cur_hi = ch->high_d;
        bool cur_en = ch->enabled;

        cJSON *ie = cJSON_GetObjectItem(json, k_en[c]);
        cJSON *il = cJSON_GetObjectItem(json, k_low[c]);
        cJSON *ih = cJSON_GetObjectItem(json, k_high[c]);
        bool has_en = ie && cJSON_IsBool(ie);
        bool has_lo = il && cJSON_IsNumber(il);
        bool has_hi = ih && cJSON_IsNumber(ih);
#ifndef CONFIG_THERMOCOUPLE_ENABLED
        if (c == 2) has_en = has_lo = has_hi = false;
#endif
        ch->keep = !(has_en || has_lo || has_hi);
        if (has_en) ch->enabled = cJSON_IsTrue(ie);
        if (has_lo && !to_tenths(il->valuedouble, &ch->low_d))  range_bad = true;
        if (has_hi && !to_tenths(ih->valuedouble, &ch->high_d)) range_bad = true;
        if (!ch->keep && (ch->enabled != cur_en || ch->low_d != cur_lo || ch->high_d != cur_hi))
            alarm_differs = true;
    }

    bool has_version = false;
    cJSON *iv = cJSON_GetObjectItem(json, "limits_version");
    if (iv) {
        if (!cJSON_IsNumber(iv) || iv->valuedouble != floor(iv->valuedouble) ||
            iv->valuedouble < 1 || iv->valuedouble > 32767) {
            cJSON_Delete(json);
            return send_json_error(req, "400 Bad Request", "Vers\xC3\xA3o inv\xC3\xA1lida");
        }
        has_version = true;
        cand.version = (uint16_t)iv->valuedouble;
    }

    if (range_bad) {
        cJSON_Delete(json);
        return send_json_error(req, "400 Bad Request", msg_for_alarm_err(ALARM_SYNC_ERR_RANGE));
    }
    alarm_sync_err_t verr = alarm_sync_validate(&cand);
    if (verr != ALARM_SYNC_OK) {
        cJSON_Delete(json);
        return send_json_error(req, "400 Bad Request", msg_for_alarm_err(verr));
    }

    // ---- 2. Non-alarm fields (existing flow) ----
    if ((item = cJSON_GetObjectItem(json, "interval")) && cJSON_IsNumber(item)) {
        config_set_sensor_interval((uint32_t)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "temp_correction")) && cJSON_IsNumber(item)) {
        config_set_temp_correction((float)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "hum_correction")) && cJSON_IsNumber(item)) {
        config_set_hum_correction((float)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "device_name")) && cJSON_IsString(item)) {
        config_set_device_name(item->valuestring);
    }
#ifdef CONFIG_THERMOCOUPLE_ENABLED
    if ((item = cJSON_GetObjectItem(json, "thermocouple_enabled")) && cJSON_IsBool(item)) {
        config_set_thermocouple_enabled(cJSON_IsTrue(item));
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_max_temp")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_max_temp((float)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_sck_pin")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_sck_pin((uint8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_so_pin")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_so_pin((uint8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_cs_pin")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_cs_pin((uint8_t)item->valueint);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_min_temp")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_min_temp((float)item->valuedouble);
    }
    if ((item = cJSON_GetObjectItem(json, "thermocouple_correction")) && cJSON_IsNumber(item)) {
        config_set_thermocouple_correction((float)item->valuedouble);
    }
#endif
    if ((item = cJSON_GetObjectItem(json, "buzzer_volume")) && cJSON_IsNumber(item)) {
        uint8_t vol = (uint8_t)item->valueint;
        config_set_buzzer_volume(vol);
        buzzer_set_volume(vol);
        buzzer_beep(100);  // Test beep at new volume
    }

    cJSON_Delete(json);

    // ---- 3. Single save ----
    esp_err_t save_err;
    if (has_version || alarm_differs) {
        save_err = config_apply_alarm_limits(&cand, !has_version);
        alarm_events_set_error(save_err == ESP_OK ? ALARM_SYNC_OK : ALARM_SYNC_ERR_SAVE);
    } else {
        save_err = config_save();
    }
    if (save_err != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", "Falha ao gravar");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", "Sensor config saved");

    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}
