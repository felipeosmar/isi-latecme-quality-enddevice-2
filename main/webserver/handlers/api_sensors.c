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
    cJSON_AddBoolToObject(root, "thermocouple_valid", data.thermocouple_valid);
    cJSON_AddNumberToObject(root, "thermocouple_temp", data.thermocouple_temp);
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

// GET /api/sensors/config
esp_err_t api_sensors_config_get_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return send_unauthorized(req);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "interval", config_get_sensor_interval());
    cJSON_AddNumberToObject(root, "temp_correction", config_get_temp_correction());
    cJSON_AddNumberToObject(root, "hum_correction", config_get_hum_correction());
    cJSON_AddStringToObject(root, "device_name", config_get_device_name());
    cJSON_AddBoolToObject(root, "thermocouple_enabled", config_get_thermocouple_enabled());
    cJSON_AddNumberToObject(root, "thermocouple_max_temp", config_get_thermocouple_max_temp());
    cJSON_AddNumberToObject(root, "thermocouple_sck_pin", config_get_thermocouple_sck_pin());
    cJSON_AddNumberToObject(root, "thermocouple_so_pin", config_get_thermocouple_so_pin());
    cJSON_AddNumberToObject(root, "thermocouple_cs_pin", config_get_thermocouple_cs_pin());
    cJSON_AddNumberToObject(root, "thermocouple_min_temp", config_get_thermocouple_min_temp());
    cJSON_AddNumberToObject(root, "thermocouple_correction", config_get_thermocouple_correction());
    cJSON_AddNumberToObject(root, "buzzer_volume", config_get_buzzer_volume());
    cJSON_AddBoolToObject(root, "alarm_temp_enabled", config_get_alarm_temp_enabled());
    cJSON_AddNumberToObject(root, "alarm_temp_low",   config_get_alarm_temp_low());
    cJSON_AddNumberToObject(root, "alarm_temp_high",  config_get_alarm_temp_high());
    cJSON_AddBoolToObject(root, "alarm_hum_enabled",  config_get_alarm_hum_enabled());
    cJSON_AddNumberToObject(root, "alarm_hum_low",    config_get_alarm_hum_low());
    cJSON_AddNumberToObject(root, "alarm_hum_high",   config_get_alarm_hum_high());
    cJSON_AddBoolToObject(root, "alarm_tc_enabled",   config_get_alarm_tc_enabled());
    cJSON_AddNumberToObject(root, "alarm_tc_low",     config_get_alarm_tc_low());
    cJSON_AddNumberToObject(root, "alarm_tc_high",    config_get_alarm_tc_high());

    char *json_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(root);
    return ESP_OK;
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
    if ((item = cJSON_GetObjectItem(json, "buzzer_volume")) && cJSON_IsNumber(item)) {
        uint8_t vol = (uint8_t)item->valueint;
        config_set_buzzer_volume(vol);
        buzzer_set_volume(vol);
        buzzer_beep(100);  // Test beep at new volume
    }
    if ((item = cJSON_GetObjectItem(json, "alarm_temp_enabled")) && cJSON_IsBool(item))
        config_set_alarm_temp_enabled(cJSON_IsTrue(item));
    if ((item = cJSON_GetObjectItem(json, "alarm_temp_low")) && cJSON_IsNumber(item))
        config_set_alarm_temp_low((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_temp_high")) && cJSON_IsNumber(item))
        config_set_alarm_temp_high((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_hum_enabled")) && cJSON_IsBool(item))
        config_set_alarm_hum_enabled(cJSON_IsTrue(item));
    if ((item = cJSON_GetObjectItem(json, "alarm_hum_low")) && cJSON_IsNumber(item))
        config_set_alarm_hum_low((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_hum_high")) && cJSON_IsNumber(item))
        config_set_alarm_hum_high((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_enabled")) && cJSON_IsBool(item))
        config_set_alarm_tc_enabled(cJSON_IsTrue(item));
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_low")) && cJSON_IsNumber(item))
        config_set_alarm_tc_low((float)item->valuedouble);
    if ((item = cJSON_GetObjectItem(json, "alarm_tc_high")) && cJSON_IsNumber(item))
        config_set_alarm_tc_high((float)item->valuedouble);

    cJSON_Delete(json);

    // Save config
    config_save();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    cJSON_AddStringToObject(resp, "message", "Sensor config saved");

    char *json_str = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    free(json_str);
    cJSON_Delete(resp);
    return ESP_OK;
}
