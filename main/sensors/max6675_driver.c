/**
 * @file max6675_driver.c
 * @brief MAX6675 thermocouple temperature sensor driver (SPI bit-bang)
 *
 * Protocol:
 * - Pull CS low to start read
 * - Clock 16 bits out of SO (MSB first)
 * - Pull CS high to end read
 *
 * Data format (16 bits):
 * - Bit 15: dummy sign bit (always 0)
 * - Bits 14-3: 12-bit temperature data (0.25°C per LSB)
 * - Bit 2: open thermocouple flag (1 = open / not connected)
 * - Bit 1: device ID (always 0 for MAX6675)
 * - Bit 0: tri-state (always 0 when CS is low)
 *
 * Minimum 220ms between conversions.
 */

#include "max6675_driver.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAX6675";

// Minimum time between reads (microseconds)
#define MAX6675_MIN_READ_INTERVAL_US    (220 * 1000)

// Temperature range for this application
#define MAX6675_TEMP_MIN    0.0f
#define MAX6675_TEMP_MAX    200.0f

// SPI bit-bang clock half-period (microseconds)
#define SPI_CLK_DELAY_US    5

// Forward declaration
static uint16_t max6675_read_raw(void);

static int s_sck_gpio = -1;
static int s_so_gpio = -1;
static int s_cs_gpio = -1;
static bool s_initialized = false;
static bool s_connected = false;
static int64_t s_last_read_us = 0;

esp_err_t max6675_init(int sck_gpio, int so_gpio, int cs_gpio)
{
    esp_err_t ret;

    // Validate GPIO numbers before configuring
    if (sck_gpio < 0 || sck_gpio >= GPIO_NUM_MAX ||
        so_gpio < 0 || so_gpio >= GPIO_NUM_MAX ||
        cs_gpio < 0 || cs_gpio >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid GPIO numbers: SCK=%d, SO=%d, CS=%d", sck_gpio, so_gpio, cs_gpio);
        return ESP_ERR_INVALID_ARG;
    }

    // Check for pins that are reserved for flash (6-11) or strapping (0, 2, 5, 12, 15)
    for (int i = 0; i < 3; i++) {
        int pin = (i == 0) ? sck_gpio : (i == 1) ? so_gpio : cs_gpio;
        if (pin >= 6 && pin <= 11) {
            ESP_LOGE(TAG, "GPIO %d is reserved for flash, cannot use for MAX6675", pin);
            return ESP_ERR_INVALID_ARG;
        }
    }

    ESP_LOGI(TAG, "Configuring GPIOs: SCK=%d(out), SO=%d(in), CS=%d(out)", sck_gpio, so_gpio, cs_gpio);

    // Configure SCK as output
    gpio_config_t sck_cfg = {
        .pin_bit_mask = (1ULL << sck_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&sck_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure SCK GPIO%d: %s", sck_gpio, esp_err_to_name(ret));
        return ret;
    }

    // Configure SO as input
    gpio_config_t so_cfg = {
        .pin_bit_mask = (1ULL << so_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&so_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure SO GPIO%d: %s", so_gpio, esp_err_to_name(ret));
        return ret;
    }

    // Configure CS as output
    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << cs_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&cs_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure CS GPIO%d: %s", cs_gpio, esp_err_to_name(ret));
        return ret;
    }

    // Set initial states: CS high (deselected), SCK low
    gpio_set_level(cs_gpio, 1);
    gpio_set_level(sck_gpio, 0);

    s_sck_gpio = sck_gpio;
    s_so_gpio = so_gpio;
    s_cs_gpio = cs_gpio;
    s_initialized = true;
    s_connected = false;
    s_last_read_us = 0;

    ESP_LOGI(TAG, "MAX6675 initialized (SCK=%d, SO=%d, CS=%d)", sck_gpio, so_gpio, cs_gpio);

    // Verify GPIO levels after init
    ESP_LOGI(TAG, "Post-init levels: SCK=%d, SO=%d, CS=%d",
             gpio_get_level(sck_gpio), gpio_get_level(so_gpio), gpio_get_level(cs_gpio));

    // Do a test read immediately (use vTaskDelay to avoid triggering interrupt WDT)
    vTaskDelay(pdMS_TO_TICKS(250));
    uint16_t test_raw = max6675_read_raw();
    if (test_raw == 0x0000) {
        ESP_LOGW(TAG, "Test read returned 0x0000 — SO line may be stuck LOW (check wiring)");
    } else if (test_raw == 0xFFFF) {
        ESP_LOGW(TAG, "Test read returned 0xFFFF — SO line may be stuck HIGH (check wiring/no module?)");
    } else if (test_raw & 0x04) {
        ESP_LOGW(TAG, "Test read: thermocouple OPEN CIRCUIT (check thermocouple wires)");
    } else {
        float test_temp = ((test_raw >> 3) & 0x0FFF) * 0.25f;
        ESP_LOGI(TAG, "Test read OK: %.2f°C", test_temp);
    }

    return ESP_OK;
}

/**
 * @brief Read 16 raw bits from MAX6675 via SPI bit-bang
 */
static uint16_t max6675_read_raw(void)
{
    uint16_t data = 0;

    // Log pin states before read
    ESP_LOGD(TAG, "SPI read start (SCK=%d, SO=%d, CS=%d)", s_sck_gpio, s_so_gpio, s_cs_gpio);
    ESP_LOGD(TAG, "Pre-read levels: SCK=%d, SO=%d, CS=%d",
             gpio_get_level(s_sck_gpio), gpio_get_level(s_so_gpio), gpio_get_level(s_cs_gpio));

    // CS low - start read
    gpio_set_level(s_cs_gpio, 0);
    esp_rom_delay_us(SPI_CLK_DELAY_US);

    ESP_LOGD(TAG, "CS pulled LOW, SO level=%d", gpio_get_level(s_so_gpio));

    // Clock 16 bits (MSB first)
    for (int i = 15; i >= 0; i--) {
        gpio_set_level(s_sck_gpio, 1);
        esp_rom_delay_us(SPI_CLK_DELAY_US);

        if (gpio_get_level(s_so_gpio)) {
            data |= (1 << i);
        }

        gpio_set_level(s_sck_gpio, 0);
        esp_rom_delay_us(SPI_CLK_DELAY_US);
    }

    // CS high - end read
    gpio_set_level(s_cs_gpio, 1);

    ESP_LOGD(TAG, "SPI raw: 0x%04X, temp_raw=%u (%.2f°C), open=%d",
             data, (data >> 3) & 0x0FFF, ((data >> 3) & 0x0FFF) * 0.25f,
             (data >> 2) & 1);

    return data;
}

esp_err_t max6675_read(float *temperature)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!temperature) {
        return ESP_ERR_INVALID_ARG;
    }

    // Enforce minimum interval between reads (use vTaskDelay to avoid WDT)
    int64_t now_us = esp_timer_get_time();
    int64_t elapsed = now_us - s_last_read_us;
    if (s_last_read_us > 0 && elapsed < MAX6675_MIN_READ_INTERVAL_US) {
        int64_t wait_ms = (MAX6675_MIN_READ_INTERVAL_US - elapsed) / 1000 + 1;
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }

    uint16_t raw = max6675_read_raw();
    s_last_read_us = esp_timer_get_time();

    // Check open thermocouple bit (bit 2)
    if (raw & 0x04) {
        ESP_LOGW(TAG, "Thermocouple open circuit detected (raw=0x%04X)", raw);
        s_connected = false;
        return ESP_ERR_INVALID_STATE;
    }

    // Extract temperature: bits 14-3 (12 bits), resolution 0.25°C
    uint16_t temp_raw = (raw >> 3) & 0x0FFF;
    float temp_c = temp_raw * 0.25f;

    // Clamp to application range
    if (temp_c < MAX6675_TEMP_MIN) temp_c = MAX6675_TEMP_MIN;
    if (temp_c > MAX6675_TEMP_MAX) temp_c = MAX6675_TEMP_MAX;

    *temperature = temp_c;
    s_connected = true;

    return ESP_OK;
}

bool max6675_is_connected(void)
{
    return s_initialized && s_connected;
}
