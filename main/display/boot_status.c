/**
 * @file boot_status.c
 * @brief Boot step list: logs each step and draws it on the OLED
 */

#include "boot_status.h"
#include "oled_display.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_app_desc.h"

static const char *TAG = "BOOT";

#define MAX_STEPS       12
#define TEXT_LEN        18                  // max chars for name and detail
#define COLS            21                  // OLED text columns
#define VISIBLE_LINES   7                   // pages 1-7
#define MAX_LINES       (MAX_STEPS * 2)
#define LINE_BUF        (COLS + 1)

typedef struct {
    char name[TEXT_LEN + 1];
    char detail[TEXT_LEN + 1];
    boot_step_state_t state;
} step_t;

static SemaphoreHandle_t s_mutex = NULL;
static step_t s_steps[MAX_STEPS];
static int s_count = 0;
static bool s_finished = false;
static char s_header[LINE_BUF] = " BOOT";

static const char *state_tag(boot_step_state_t st)
{
    switch (st) {
    case BOOT_STEP_OK:    return "ok";
    case BOOT_STEP_ERROR: return "ER";
    default:              return "..";
    }
}

static void copy_trunc(char *dst, const char *src)
{
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, TEXT_LEN);
    dst[TEXT_LEN] = '\0';
}

static void build_header(void)
{
    const char *ver = esp_app_get_description()->version;
    const char *left = " BOOT";
    size_t left_len = strlen(left);
    size_t ver_len = strlen(ver);
    size_t room = COLS - left_len;

    // Truncate the version from the left if it does not fit
    if (ver_len > room) {
        ver += ver_len - room;
        ver_len = room;
    }
    memset(s_header, ' ', COLS);
    memcpy(s_header, left, left_len);
    memcpy(s_header + COLS - ver_len, ver, ver_len);
    s_header[COLS] = '\0';
}

// Draw the last VISIBLE_LINES lines. Copies state under the mutex, draws outside it.
static void redraw(void)
{
    char buf[MAX_LINES][LINE_BUF];
    char local_hdr[LINE_BUF];
    const char *ptrs[VISIBLE_LINES];
    char out[VISIBLE_LINES][LINE_BUF];
    int total = 0;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    memcpy(local_hdr, s_header, sizeof(local_hdr));
    for (int i = 0; i < s_count; i++) {
        snprintf(buf[total++], LINE_BUF, "%s %s", state_tag(s_steps[i].state), s_steps[i].name);
        if (s_steps[i].detail[0]) {
            snprintf(buf[total++], LINE_BUF, "   %s", s_steps[i].detail);
        }
    }
    int first = total > VISIBLE_LINES ? total - VISIBLE_LINES : 0;
    int n = total - first;
    for (int i = 0; i < n; i++) {
        memcpy(out[i], buf[first + i], LINE_BUF);
        ptrs[i] = out[i];
    }
    xSemaphoreGive(s_mutex);

    oled_display_show_boot(local_hdr, ptrs, (size_t)n);
}

static void log_step(const step_t *s)
{
    const char *tag = state_tag(s->state);
    esp_log_level_t lvl = s->state == BOOT_STEP_ERROR ? ESP_LOG_WARN : ESP_LOG_INFO;

    if (s->detail[0]) {
        ESP_LOG_LEVEL(lvl, TAG, "[%s] %s (%s)", tag, s->name, s->detail);
    } else {
        ESP_LOG_LEVEL(lvl, TAG, "[%s] %s", tag, s->name);
    }
}

void boot_status_init(void)
{
    if (s_mutex) {
        return;
    }
    s_mutex = xSemaphoreCreateMutex();
    s_count = 0;
    s_finished = false;
    build_header();
}

int boot_status_begin(const char *name, const char *detail)
{
    if (!s_mutex || !name) {
        return -1;
    }
    step_t snap;
    int id = -1;
    bool finished;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    finished = s_finished;
    if (s_count < MAX_STEPS) {
        id = s_count++;
        copy_trunc(s_steps[id].name, name);
        copy_trunc(s_steps[id].detail, detail);
        s_steps[id].state = BOOT_STEP_RUNNING;
        snap = s_steps[id];
    }
    xSemaphoreGive(s_mutex);

    if (id < 0) {
        ESP_LOGW(TAG, "step list full, dropping '%s'", name);
        return -1;
    }
    log_step(&snap);
    if (!finished) {
        redraw();
    }
    return id;
}

void boot_status_end(int step, boot_step_state_t state, const char *detail)
{
    if (!s_mutex) {
        return;
    }
    step_t snap;
    bool finished;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (step < 0 || step >= s_count) {
        xSemaphoreGive(s_mutex);
        return;
    }
    s_steps[step].state = state;
    if (detail) {
        copy_trunc(s_steps[step].detail, detail);
    }
    snap = s_steps[step];
    finished = s_finished;
    xSemaphoreGive(s_mutex);

    log_step(&snap);
    if (!finished) {
        redraw();
    }
}

void boot_status_finish(void)
{
    if (!s_mutex) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_finished = true;
    xSemaphoreGive(s_mutex);
}

bool boot_status_is_finished(void)
{
    return s_finished;
}
