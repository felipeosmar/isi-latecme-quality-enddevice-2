/**
 * @file button_handler.c
 * @brief GPIO25 button: short press (navigate/acknowledge) and long press (factory reset)
 *
 * Uses falling-edge ISR + task notification for responsive input.
 * Debounce: 50ms. Short press: < 2000ms. Long press: ≥ 30000ms.
 */

#include "button_handler.h"
#include "alarm_manager.h"
#include "oled_display.h"
#include "config_manager.h"
#include "buzzer.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUTTON";

#define DEBOUNCE_US         50000   // 50ms in microseconds
#define SHORT_PRESS_MAX_MS  2000
#define LONG_PRESS_MS       30000

static TaskHandle_t    s_task_handle = NULL;
static volatile int64_t s_last_isr_us = 0;
static bool            s_initialized = false;

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    int64_t now = esp_timer_get_time();
    if ((now - s_last_isr_us) < DEBOUNCE_US) return;
    s_last_isr_us = now;

    BaseType_t higher = pdFALSE;
    vTaskNotifyGiveFromISR(s_task_handle, &higher);
    portYIELD_FROM_ISR(higher);
}

static void button_task(void *param)
{
    while (1) {
        // Block until ISR fires (button pressed)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        int64_t press_start_us = esp_timer_get_time();
        bool long_press_fired  = false;
        uint8_t oled_tick      = 0;

        // Poll until button is released or long press threshold reached
        while (gpio_get_level(BUTTON_GPIO) == 0) {
            int64_t held_ms = (esp_timer_get_time() - press_start_us) / 1000;

            if (!long_press_fired && held_ms >= LONG_PRESS_MS) {
                long_press_fired = true;
                ESP_LOGW(TAG, "Long press: factory reset");
                alarm_manager_acknowledge();   // silence siren before beeping
                buzzer_beep_pattern(3, 200, 100);
                config_factory_reset();
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }

            // Show progress bar on OLED after 1s of holding (update every ~240ms)
            if (held_ms >= 1000 && (oled_tick++ % 12) == 0) {
                uint8_t pct = (uint8_t)((uint32_t)held_ms * 100 / LONG_PRESS_MS);
                if (pct > 100) pct = 100;
                oled_display_show_factory_reset(pct);
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }

        if (!long_press_fired) {
            int64_t held_ms = (esp_timer_get_time() - press_start_us) / 1000;
            if (held_ms < SHORT_PRESS_MAX_MS) {
                if (alarm_manager_is_active()) {
                    ESP_LOGI(TAG, "Short press: acknowledge alarm");
                    alarm_manager_acknowledge();
                } else {
                    ESP_LOGI(TAG, "Short press: next page");
                    oled_display_next_page();
                }
            } else {
                ESP_LOGI(TAG, "Press ignored (held %lldms, not long enough for reset)", held_ms);
            }
        }
    }
}

esp_err_t button_handler_init(void)
{
    if (s_initialized) return ESP_OK;

    gpio_config_t io_conf = {
        .pin_bit_mask  = (1ULL << BUTTON_GPIO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_NEGEDGE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t task_ret = xTaskCreatePinnedToCore(
        button_task, "button", 3072, NULL, 4, &s_task_handle, 0);
    if (task_ret != pdPASS || s_task_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create button task");
        return ESP_ERR_NO_MEM;
    }

    ret = gpio_isr_handler_add(BUTTON_GPIO, gpio_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Button handler initialized (GPIO%d)", BUTTON_GPIO);
    return ESP_OK;
}
