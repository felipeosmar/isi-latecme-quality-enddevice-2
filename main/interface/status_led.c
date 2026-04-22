#include "status_led.h"
#include <string.h>
#include <stdlib.h>
#include "led_strip.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config_manager.h"

static const char *TAG = "STATUS_LED";

static led_strip_handle_t s_led_strip = NULL;
static bool s_initialized = false;
static int64_t s_last_blink_time = 0;

// Heap threshold matching health_monitor.c
#define HEAP_WARNING_THRESHOLD 20000

/**
 * @brief Parse "#RRGGBB" hex string into r, g, b components
 */
static void parse_hex_color(const char *hex, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!hex || hex[0] != '#' || strlen(hex) < 7) {
        *r = *g = *b = 255; // fallback white
        return;
    }
    unsigned long val = strtoul(hex + 1, NULL, 16);
    *r = (val >> 16) & 0xFF;
    *g = (val >> 8) & 0xFF;
    *b = val & 0xFF;
}

/**
 * @brief Apply brightness scaling to a color component
 */
static uint8_t apply_brightness(uint8_t component, uint8_t brightness_pct)
{
    return (uint8_t)(((uint32_t)component * brightness_pct) / 100);
}

/**
 * @brief Set LED to color with brightness applied, then refresh
 */
static void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t brightness = config_get_led_brightness();
    led_strip_set_pixel(s_led_strip, 0,
                        apply_brightness(r, brightness),
                        apply_brightness(g, brightness),
                        apply_brightness(b, brightness));
    led_strip_refresh(s_led_strip);
}

/**
 * @brief Turn LED off
 */
static void led_off(void)
{
    led_strip_clear(s_led_strip);
    led_strip_refresh(s_led_strip);
}

/**
 * @brief Single blink: on for on_ms, then off
 */
static void blink_once(uint8_t r, uint8_t g, uint8_t b, uint32_t on_ms)
{
    led_set_color(r, g, b);
    vTaskDelay(pdMS_TO_TICKS(on_ms));
    led_off();
}

/**
 * @brief Determine LED state from health data
 */
static led_state_t evaluate_state(const system_health_t *health)
{
    // Priority 1: System error
    if (!health->filesystem_ok ||
        health->total_error_count > 0 ||
        health->free_heap < HEAP_WARNING_THRESHOLD) {
        return LED_STATE_ERROR;
    }

    // Priority 2: WiFi disconnected
#if CONFIG_WIFI_ENABLED
    if (!health->wifi_connected) {
        return LED_STATE_WIFI_DISCONNECTED;
    }
#endif

    // Priority 3: LoRaWAN not configured (keys empty)
    const char *dev_eui = config_get_dev_eui();
    const char *app_key = config_get_app_key();
    const char *join_eui = config_get_join_eui();
    if (strlen(dev_eui) == 0 || strlen(app_key) == 0 || strlen(join_eui) == 0) {
        return LED_STATE_LORAWAN_NOT_CONFIGURED;
    }

    // Priority 4: All OK
    return LED_STATE_NORMAL;
}

esp_err_t status_led_init(void)
{
    if (s_initialized) return ESP_OK;

    if (!config_get_led_enabled()) {
        ESP_LOGI(TAG, "Status LED disabled in config");
        return ESP_OK;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = STATUS_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    led_off();
    s_initialized = true;
    s_last_blink_time = 0;
    ESP_LOGI(TAG, "Status LED initialized on GPIO%d", STATUS_LED_GPIO);
    return ESP_OK;
}

void status_led_update(const system_health_t *health)
{
    if (!s_initialized || !s_led_strip) return;
    if (!config_get_led_enabled()) return;

    led_state_t state = evaluate_state(health);
    uint8_t r, g, b;

    switch (state) {
        case LED_STATE_ERROR: {
            parse_hex_color(config_get_led_color_error(), &r, &g, &b);
            // Double blink pattern
            blink_once(r, g, b, 100);
            vTaskDelay(pdMS_TO_TICKS(100));
            blink_once(r, g, b, 100);
            s_last_blink_time = esp_timer_get_time();
            break;
        }
        case LED_STATE_WIFI_DISCONNECTED: {
            parse_hex_color(config_get_led_color_wifi(), &r, &g, &b);
            blink_once(r, g, b, 200);
            s_last_blink_time = esp_timer_get_time();
            break;
        }
        case LED_STATE_LORAWAN_NOT_CONFIGURED: {
            parse_hex_color(config_get_led_color_lorawan(), &r, &g, &b);
            blink_once(r, g, b, 200);
            s_last_blink_time = esp_timer_get_time();
            break;
        }
        case LED_STATE_NORMAL: {
            int64_t now = esp_timer_get_time();
            uint32_t interval_us = config_get_led_blink_interval_ms() * 1000;
            if (s_last_blink_time == 0 || (now - s_last_blink_time) >= interval_us) {
                parse_hex_color(config_get_led_color_normal(), &r, &g, &b);
                blink_once(r, g, b, 100);
                s_last_blink_time = now;
            }
            break;
        }
    }
}

void status_led_deinit(void)
{
    if (s_led_strip) {
        led_off();
        led_strip_del(s_led_strip);
        s_led_strip = NULL;
    }
    s_initialized = false;
}
