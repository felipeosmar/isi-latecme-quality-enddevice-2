/**
 * @file buzzer.c
 * @brief Buzzer driver implementation using LEDC PWM for tone generation
 *
 * Uses GPIO4 → BC817-25 transistor → passive buzzer.
 * Generates audible tone via PWM oscillation (LEDC peripheral).
 * Default frequency: 2700 Hz (good audibility for small buzzers).
 */

#include "buzzer.h"

#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "BUZZER";
static bool initialized = false;
static uint8_t s_volume = 30;  // default 30%

// LEDC configuration
#define BUZZER_LEDC_TIMER       LEDC_TIMER_0
#define BUZZER_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BUZZER_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BUZZER_DUTY_RESOLUTION  LEDC_TIMER_10_BIT
#define BUZZER_DEFAULT_FREQ     2700    // Hz — good for small passive buzzers
#define BUZZER_DUTY_50PCT       512     // 50% duty cycle (10-bit: 1024/2)

/**
 * @brief Calculate LEDC duty from volume percentage
 * Maps 0-100% to 0-512 (50% duty = max volume for passive buzzer)
 */
static uint32_t volume_to_duty(void)
{
    if (s_volume == 0) return 0;
    // 50% duty cycle (512) = maximum volume for passive buzzer
    // Scale linearly: volume 100 → duty 512, volume 1 → duty ~5
    return (uint32_t)(((uint32_t)s_volume * BUZZER_DUTY_50PCT) / 100);
}

void buzzer_set_volume(uint8_t volume)
{
    if (volume > 100) volume = 100;
    s_volume = volume;
    ESP_LOGI(TAG, "Buzzer volume set to %u%%", volume);
}

uint8_t buzzer_get_volume(void)
{
    return s_volume;
}

esp_err_t buzzer_init(void)
{
    if (initialized) {
        return ESP_OK;
    }

    // Configure LEDC timer
    ledc_timer_config_t timer_cfg = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = BUZZER_DUTY_RESOLUTION,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = BUZZER_DEFAULT_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure LEDC channel (start with duty 0 = silent)
    ledc_channel_config_t ch_cfg = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };

    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    initialized = true;
    ESP_LOGI(TAG, "Buzzer initialized on GPIO%d (PWM %d Hz)", BUZZER_GPIO, BUZZER_DEFAULT_FREQ);
    return ESP_OK;
}

void buzzer_on(void)
{
    if (!initialized || s_volume == 0) return;
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, volume_to_duty());
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

void buzzer_off(void)
{
    if (!initialized) return;
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!initialized || s_volume == 0) return;

    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, volume_to_duty());
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

void buzzer_beep(uint32_t duration_ms)
{
    buzzer_tone(BUZZER_DEFAULT_FREQ, duration_ms);
}

void buzzer_beep_pattern(uint8_t count, uint32_t on_ms, uint32_t off_ms)
{
    if (!initialized || count == 0) return;

    for (uint8_t i = 0; i < count; i++) {
        buzzer_beep(on_ms);

        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
}
