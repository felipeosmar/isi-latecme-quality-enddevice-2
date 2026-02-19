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

static const char *TAG = "MAX6675";

// Minimum time between reads (microseconds)
#define MAX6675_MIN_READ_INTERVAL_US    (220 * 1000)

// Temperature range for this application
#define MAX6675_TEMP_MIN    0.0f
#define MAX6675_TEMP_MAX    200.0f

// SPI bit-bang clock half-period (microseconds)
#define SPI_CLK_DELAY_US    5

static int s_sck_gpio = -1;
static int s_so_gpio = -1;
static int s_cs_gpio = -1;
static bool s_initialized = false;
static bool s_connected = false;
static int64_t s_last_read_us = 0;

esp_err_t max6675_init(int sck_gpio, int so_gpio, int cs_gpio)
{
    esp_err_t ret;

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
    return ESP_OK;
}

/**
 * @brief Read 16 raw bits from MAX6675 via SPI bit-bang
 */
static uint16_t max6675_read_raw(void)
{
    uint16_t data = 0;

    // CS low - start read
    gpio_set_level(s_cs_gpio, 0);
    esp_rom_delay_us(SPI_CLK_DELAY_US);

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

    // Enforce minimum interval between reads
    int64_t now_us = esp_timer_get_time();
    int64_t elapsed = now_us - s_last_read_us;
    if (s_last_read_us > 0 && elapsed < MAX6675_MIN_READ_INTERVAL_US) {
        int64_t wait_us = MAX6675_MIN_READ_INTERVAL_US - elapsed;
        esp_rom_delay_us((uint32_t)wait_us);
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
