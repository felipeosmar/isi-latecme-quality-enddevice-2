/**
 * @file sensor_manager.c
 * @brief Sensor manager - I2C bus init, auto-detection, periodic reads
 *
 * Initializes I2C bus on GPIO21 (SDA) / GPIO22 (SCL).
 * Auto-detects I2C sensors: SHT20 -> SHT3x -> AM2315C.
 * Reads MAX6675 thermocouple if enabled in config.
 * Stores results in mutex-protected sensor_data_t.
 */

#include "sensor_manager.h"
#include "sht20_driver.h"
#include "sht3x_driver.h"
#include "am2315c_driver.h"
#include "max6675_driver.h"
#include "config_manager.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "SENSOR_MGR";

// I2C pins
#define I2C_SDA_GPIO    21
#define I2C_SCL_GPIO    22

// Sensor type enum
typedef enum {
    SENSOR_NONE = 0,
    SENSOR_SHT20,
    SENSOR_SHT3X,
    SENSOR_AM2315C,
} sensor_type_t;

// Function pointers for the detected I2C sensor
typedef esp_err_t (*sensor_read_fn)(float *temperature, float *humidity);

static i2c_master_bus_handle_t i2c_bus = NULL;
static sensor_type_t detected_sensor = SENSOR_NONE;
static sensor_read_fn sensor_read = NULL;
static sensor_data_t sensor_data = {0};
static SemaphoreHandle_t data_mutex = NULL;
static bool initialized = false;

/**
 * @brief Auto-detect I2C sensor
 */
static void detect_sensor(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus for sensors...");

    // Try SHT20 (0x40)
    if (sht20_probe(i2c_bus) == ESP_OK) {
        ESP_LOGI(TAG, "SHT20 detected at 0x%02X", SHT20_I2C_ADDR);
        if (sht20_init(i2c_bus) == ESP_OK) {
            detected_sensor = SENSOR_SHT20;
            sensor_read = sht20_read;
            strncpy(sensor_data.sensor_name, "SHT20", sizeof(sensor_data.sensor_name));
            return;
        }
    }

    // Try SHT3x (0x44)
    if (sht3x_probe(i2c_bus) == ESP_OK) {
        ESP_LOGI(TAG, "SHT3x detected at 0x%02X", SHT3X_I2C_ADDR);
        if (sht3x_init(i2c_bus) == ESP_OK) {
            detected_sensor = SENSOR_SHT3X;
            sensor_read = sht3x_read;
            strncpy(sensor_data.sensor_name, "SHT3x", sizeof(sensor_data.sensor_name));
            return;
        }
    }

    // Try AM2315C/AHT20 (0x38)
    if (am2315c_probe(i2c_bus) == ESP_OK) {
        ESP_LOGI(TAG, "AM2315C/AHT20 detected at 0x%02X", AM2315C_I2C_ADDR);
        if (am2315c_init(i2c_bus) == ESP_OK) {
            detected_sensor = SENSOR_AM2315C;
            sensor_read = am2315c_read;
            strncpy(sensor_data.sensor_name, "AM2315C", sizeof(sensor_data.sensor_name));
            return;
        }
    }

    ESP_LOGW(TAG, "No I2C temperature/humidity sensor detected");
    detected_sensor = SENSOR_NONE;
    sensor_read = NULL;
    strncpy(sensor_data.sensor_name, "None", sizeof(sensor_data.sensor_name));
}

esp_err_t sensor_manager_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    data_mutex = xSemaphoreCreateMutex();
    if (!data_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize I2C master bus
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d)", I2C_SDA_GPIO, I2C_SCL_GPIO);

    // Auto-detect I2C sensor
    detect_sensor();

    // Initialize MAX6675 thermocouple if enabled
    if (config_get_thermocouple_enabled()) {
        int sck = config_get_thermocouple_sck_pin();
        int so = config_get_thermocouple_so_pin();
        int cs = config_get_thermocouple_cs_pin();
        ESP_LOGI(TAG, "Initializing MAX6675 thermocouple (SCK=%d, SO=%d, CS=%d)...", sck, so, cs);
        esp_err_t tc_ret = max6675_init(sck, so, cs);
        if (tc_ret == ESP_OK) {
            ESP_LOGI(TAG, "MAX6675 initialized successfully");
        } else {
            ESP_LOGW(TAG, "MAX6675 init failed: %s (continuing without thermocouple)", esp_err_to_name(tc_ret));
        }
    } else {
        ESP_LOGI(TAG, "Thermocouple disabled in config, skipping MAX6675 init");
    }

    memset(&sensor_data, 0, sizeof(sensor_data));
    if (detected_sensor != SENSOR_NONE) {
        switch (detected_sensor) {
            case SENSOR_SHT20:   strncpy(sensor_data.sensor_name, "SHT20", sizeof(sensor_data.sensor_name)); break;
            case SENSOR_SHT3X:   strncpy(sensor_data.sensor_name, "SHT3x", sizeof(sensor_data.sensor_name)); break;
            case SENSOR_AM2315C: strncpy(sensor_data.sensor_name, "AM2315C", sizeof(sensor_data.sensor_name)); break;
            default: break;
        }
    } else {
        strncpy(sensor_data.sensor_name, "None", sizeof(sensor_data.sensor_name));
    }

    initialized = true;
    ESP_LOGI(TAG, "Sensor manager initialized (sensor: %s)", sensor_data.sensor_name);
    return ESP_OK;
}

esp_err_t sensor_manager_read(void)
{
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    float temp = 0, hum = 0, tc_temp = 0;
    bool th_valid = false, tc_valid = false;

    // Read I2C sensor
    if (sensor_read != NULL) {
        esp_err_t ret = sensor_read(&temp, &hum);
        if (ret == ESP_OK) {
            temp += config_get_temp_correction();
            hum += config_get_hum_correction();
            if (hum < 0.0f) hum = 0.0f;
            if (hum > 100.0f) hum = 100.0f;
            th_valid = true;
        } else {
            ESP_LOGW(TAG, "I2C sensor read failed: %s", esp_err_to_name(ret));
        }
    }

    // Read MAX6675 thermocouple
    if (config_get_thermocouple_enabled()) {
        esp_err_t ret = max6675_read(&tc_temp);
        if (ret == ESP_OK) {
            tc_temp += config_get_thermocouple_correction();
            float min_temp = config_get_thermocouple_min_temp();
            float max_temp = config_get_thermocouple_max_temp();
            if (tc_temp < min_temp) tc_temp = min_temp;
            if (tc_temp > max_temp) tc_temp = max_temp;
            tc_valid = true;
        } else {
            ESP_LOGW(TAG, "MAX6675 read failed: %s", esp_err_to_name(ret));
        }
    }

    // Update shared data
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    sensor_data.temperature = temp;
    sensor_data.humidity = hum;
    sensor_data.thermocouple_temp = tc_temp;
    sensor_data.temp_hum_valid = th_valid;
    sensor_data.thermocouple_valid = tc_valid;
    sensor_data.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    xSemaphoreGive(data_mutex);

    if (th_valid) {
        ESP_LOGI(TAG, "Sensor: %.1f°C, %.1f%%", temp, hum);
    }
    if (tc_valid) {
        ESP_LOGI(TAG, "Thermocouple: %.1f°C", tc_temp);
    }

    return (th_valid || tc_valid) ? ESP_OK : ESP_FAIL;
}

esp_err_t sensor_manager_get_data(sensor_data_t *data)
{
    if (!data || !data_mutex) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(data_mutex, portMAX_DELAY);
    memcpy(data, &sensor_data, sizeof(sensor_data_t));
    xSemaphoreGive(data_mutex);

    return ESP_OK;
}

void *sensor_manager_get_i2c_bus(void)
{
    return (void *)i2c_bus;
}

void sensor_task(void *param)
{
    ESP_LOGI(TAG, "Sensor task started");

    vTaskDelay(pdMS_TO_TICKS(2000));

    if (sensor_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sensor manager, task exiting");
        vTaskDelete(NULL);
        return;
    }

    sensor_manager_read();

    while (1) {
        uint32_t interval_s = config_get_sensor_interval();
        if (interval_s < 5) interval_s = 5;

        vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));
        sensor_manager_read();
    }
}
