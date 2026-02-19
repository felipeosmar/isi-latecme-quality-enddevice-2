/**
 * @file buzzer.c
 * @brief Buzzer driver implementation
 *
 * Uses GPIO4 → BC817-25 transistor → buzzer (active HIGH).
 * Non-blocking patterns use vTaskDelay (requires FreeRTOS context).
 */

#include "buzzer.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "BUZZER";
static bool initialized = false;

esp_err_t buzzer_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUZZER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO%d: %s", BUZZER_GPIO, esp_err_to_name(ret));
        return ret;
    }

    gpio_set_level(BUZZER_GPIO, 0);
    initialized = true;

    ESP_LOGI(TAG, "Buzzer initialized on GPIO%d", BUZZER_GPIO);
    return ESP_OK;
}

void buzzer_on(void)
{
    if (initialized) {
        gpio_set_level(BUZZER_GPIO, 1);
    }
}

void buzzer_off(void)
{
    if (initialized) {
        gpio_set_level(BUZZER_GPIO, 0);
    }
}

void buzzer_beep(uint32_t duration_ms)
{
    if (!initialized) {
        return;
    }

    gpio_set_level(BUZZER_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    gpio_set_level(BUZZER_GPIO, 0);
}

void buzzer_beep_pattern(uint8_t count, uint32_t on_ms, uint32_t off_ms)
{
    if (!initialized || count == 0) {
        return;
    }

    for (uint8_t i = 0; i < count; i++) {
        gpio_set_level(BUZZER_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        gpio_set_level(BUZZER_GPIO, 0);

        // Pause between beeps (skip after last beep)
        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}
