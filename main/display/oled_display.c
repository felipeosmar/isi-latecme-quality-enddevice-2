/**
 * @file oled_display.c
 * @brief SSD1306 128x64 OLED display driver
 *
 * Simple framebuffer-based driver using I2C.
 * Uses a basic 6x8 font for text rendering.
 */

#include "oled_display.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "OLED";

// SSD1306 commands
#define SSD1306_CMD_DISPLAY_OFF      0xAE
#define SSD1306_CMD_DISPLAY_ON       0xAF
#define SSD1306_CMD_SET_MUX_RATIO    0xA8
#define SSD1306_CMD_SET_DISPLAY_OFFSET 0xD3
#define SSD1306_CMD_SET_START_LINE   0x40
#define SSD1306_CMD_SET_SEG_REMAP    0xA1
#define SSD1306_CMD_SET_COM_SCAN_DIR 0xC8
#define SSD1306_CMD_SET_COM_PINS     0xDA
#define SSD1306_CMD_SET_CONTRAST     0x81
#define SSD1306_CMD_ENTIRE_DISPLAY_RAM 0xA4
#define SSD1306_CMD_SET_NORMAL       0xA6
#define SSD1306_CMD_SET_CLK_DIV      0xD5
#define SSD1306_CMD_SET_CHARGE_PUMP  0x8D
#define SSD1306_CMD_SET_MEMORY_MODE  0x20
#define SSD1306_CMD_SET_COL_ADDR     0x21
#define SSD1306_CMD_SET_PAGE_ADDR    0x22

static i2c_master_dev_handle_t oled_dev = NULL;
static uint8_t framebuffer[OLED_WIDTH * OLED_HEIGHT / 8] = {0};
static oled_page_t current_page = OLED_PAGE_SENSORS;
static bool oled_initialized = false;

// Basic 6x8 font (ASCII 32-126)
static const uint8_t font_6x8[][6] = {
    {0x00,0x00,0x00,0x00,0x00,0x00}, // space
    {0x00,0x00,0x5F,0x00,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14,0x00}, // #
    {0x24,0x2A,0x7F,0x2A,0x12,0x00}, // $
    {0x23,0x13,0x08,0x64,0x62,0x00}, // %
    {0x36,0x49,0x55,0x22,0x50,0x00}, // &
    {0x00,0x05,0x03,0x00,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00,0x00}, // )
    {0x08,0x2A,0x1C,0x2A,0x08,0x00}, // *
    {0x08,0x08,0x3E,0x08,0x08,0x00}, // +
    {0x00,0x50,0x30,0x00,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08,0x00}, // -
    {0x00,0x60,0x60,0x00,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02,0x00}, // /
    {0x3E,0x51,0x49,0x45,0x3E,0x00}, // 0
    {0x00,0x42,0x7F,0x40,0x00,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46,0x00}, // 2
    {0x21,0x41,0x45,0x4B,0x31,0x00}, // 3
    {0x18,0x14,0x12,0x7F,0x10,0x00}, // 4
    {0x27,0x45,0x45,0x45,0x39,0x00}, // 5
    {0x3C,0x4A,0x49,0x49,0x30,0x00}, // 6
    {0x01,0x71,0x09,0x05,0x03,0x00}, // 7
    {0x36,0x49,0x49,0x49,0x36,0x00}, // 8
    {0x06,0x49,0x49,0x29,0x1E,0x00}, // 9
    {0x00,0x36,0x36,0x00,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00,0x00}, // ;
    {0x00,0x08,0x14,0x22,0x41,0x00}, // <
    {0x14,0x14,0x14,0x14,0x14,0x00}, // =
    {0x41,0x22,0x14,0x08,0x00,0x00}, // >
    {0x02,0x01,0x51,0x09,0x06,0x00}, // ?
    {0x32,0x49,0x79,0x41,0x3E,0x00}, // @
    {0x7E,0x11,0x11,0x11,0x7E,0x00}, // A
    {0x7F,0x49,0x49,0x49,0x36,0x00}, // B
    {0x3E,0x41,0x41,0x41,0x22,0x00}, // C
    {0x7F,0x41,0x41,0x22,0x1C,0x00}, // D
    {0x7F,0x49,0x49,0x49,0x41,0x00}, // E
    {0x7F,0x09,0x09,0x01,0x01,0x00}, // F
    {0x3E,0x41,0x41,0x51,0x32,0x00}, // G
    {0x7F,0x08,0x08,0x08,0x7F,0x00}, // H
    {0x00,0x41,0x7F,0x41,0x00,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01,0x00}, // J
    {0x7F,0x08,0x14,0x22,0x41,0x00}, // K
    {0x7F,0x40,0x40,0x40,0x40,0x00}, // L
    {0x7F,0x02,0x04,0x02,0x7F,0x00}, // M
    {0x7F,0x04,0x08,0x10,0x7F,0x00}, // N
    {0x3E,0x41,0x41,0x41,0x3E,0x00}, // O
    {0x7F,0x09,0x09,0x09,0x06,0x00}, // P
    {0x3E,0x41,0x51,0x21,0x5E,0x00}, // Q
    {0x7F,0x09,0x19,0x29,0x46,0x00}, // R
    {0x46,0x49,0x49,0x49,0x31,0x00}, // S
    {0x01,0x01,0x7F,0x01,0x01,0x00}, // T
    {0x3F,0x40,0x40,0x40,0x3F,0x00}, // U
    {0x1F,0x20,0x40,0x20,0x1F,0x00}, // V
    {0x7F,0x20,0x18,0x20,0x7F,0x00}, // W
    {0x63,0x14,0x08,0x14,0x63,0x00}, // X
    {0x03,0x04,0x78,0x04,0x03,0x00}, // Y
    {0x61,0x51,0x49,0x45,0x43,0x00}, // Z
    {0x00,0x00,0x7F,0x41,0x41,0x00}, // [
    {0x02,0x04,0x08,0x10,0x20,0x00}, // backslash
    {0x41,0x41,0x7F,0x00,0x00,0x00}, // ]
    {0x04,0x02,0x01,0x02,0x04,0x00}, // ^
    {0x40,0x40,0x40,0x40,0x40,0x00}, // _
    {0x00,0x01,0x02,0x04,0x00,0x00}, // `
    {0x20,0x54,0x54,0x54,0x78,0x00}, // a
    {0x7F,0x48,0x44,0x44,0x38,0x00}, // b
    {0x38,0x44,0x44,0x44,0x20,0x00}, // c
    {0x38,0x44,0x44,0x48,0x7F,0x00}, // d
    {0x38,0x54,0x54,0x54,0x18,0x00}, // e
    {0x08,0x7E,0x09,0x01,0x02,0x00}, // f
    {0x08,0x14,0x54,0x54,0x3C,0x00}, // g
    {0x7F,0x08,0x04,0x04,0x78,0x00}, // h
    {0x00,0x44,0x7D,0x40,0x00,0x00}, // i
    {0x20,0x40,0x44,0x3D,0x00,0x00}, // j
    {0x00,0x7F,0x10,0x28,0x44,0x00}, // k
    {0x00,0x41,0x7F,0x40,0x00,0x00}, // l
    {0x7C,0x04,0x18,0x04,0x78,0x00}, // m
    {0x7C,0x08,0x04,0x04,0x78,0x00}, // n
    {0x38,0x44,0x44,0x44,0x38,0x00}, // o
    {0x7C,0x14,0x14,0x14,0x08,0x00}, // p
    {0x08,0x14,0x14,0x18,0x7C,0x00}, // q
    {0x7C,0x08,0x04,0x04,0x08,0x00}, // r
    {0x48,0x54,0x54,0x54,0x20,0x00}, // s
    {0x04,0x3F,0x44,0x40,0x20,0x00}, // t
    {0x3C,0x40,0x40,0x20,0x7C,0x00}, // u
    {0x1C,0x20,0x40,0x20,0x1C,0x00}, // v
    {0x3C,0x40,0x30,0x40,0x3C,0x00}, // w
    {0x44,0x28,0x10,0x28,0x44,0x00}, // x
    {0x0C,0x50,0x50,0x50,0x3C,0x00}, // y
    {0x44,0x64,0x54,0x4C,0x44,0x00}, // z
    {0x00,0x08,0x36,0x41,0x00,0x00}, // {
    {0x00,0x00,0x7F,0x00,0x00,0x00}, // |
    {0x00,0x41,0x36,0x08,0x00,0x00}, // }
    {0x08,0x08,0x2A,0x1C,0x08,0x00}, // ~
};

static void oled_send_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };
    i2c_master_transmit(oled_dev, buf, 2, 100);
}

static void oled_send_cmds(const uint8_t *cmds, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        oled_send_cmd(cmds[i]);
    }
}

esp_err_t oled_display_init(void *i2c_bus_handle)
{
    if (!i2c_bus_handle) {
        ESP_LOGE(TAG, "I2C bus handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)i2c_bus_handle;

    // Probe for OLED
    esp_err_t ret = i2c_master_probe(bus, OLED_I2C_ADDR, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OLED not found at 0x%02X", OLED_I2C_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    // Add OLED device to I2C bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = 400000,
    };

    ret = i2c_master_bus_add_device(bus, &dev_cfg, &oled_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add OLED device: %s", esp_err_to_name(ret));
        return ret;
    }

    // SSD1306 initialization sequence
    const uint8_t init_cmds[] = {
        SSD1306_CMD_DISPLAY_OFF,
        SSD1306_CMD_SET_CLK_DIV, 0x80,
        SSD1306_CMD_SET_MUX_RATIO, 0x3F,       // 64 lines
        SSD1306_CMD_SET_DISPLAY_OFFSET, 0x00,
        SSD1306_CMD_SET_START_LINE,              // start line 0
        SSD1306_CMD_SET_CHARGE_PUMP, 0x14,       // enable charge pump
        SSD1306_CMD_SET_MEMORY_MODE, 0x00,       // horizontal addressing
        SSD1306_CMD_SET_SEG_REMAP,               // segment remap
        SSD1306_CMD_SET_COM_SCAN_DIR,            // COM scan direction
        SSD1306_CMD_SET_COM_PINS, 0x12,          // COM pins config
        SSD1306_CMD_SET_CONTRAST, 0xCF,
        SSD1306_CMD_ENTIRE_DISPLAY_RAM,
        SSD1306_CMD_SET_NORMAL,
        SSD1306_CMD_DISPLAY_ON,
    };

    oled_send_cmds(init_cmds, sizeof(init_cmds));

    memset(framebuffer, 0, sizeof(framebuffer));
    oled_display_update();

    oled_initialized = true;
    ESP_LOGI(TAG, "OLED display initialized (128x64)");
    return ESP_OK;
}

void oled_display_clear(void)
{
    memset(framebuffer, 0, sizeof(framebuffer));
}

static void draw_char(uint8_t x, uint8_t page, char c)
{
    if (c < 32 || c > 126) c = ' ';
    int idx = c - 32;

    for (int i = 0; i < 6; i++) {
        if (x + i >= OLED_WIDTH) break;
        framebuffer[page * OLED_WIDTH + x + i] = font_6x8[idx][i];
    }
}

void oled_display_text(uint8_t x, uint8_t page, const char *text)
{
    if (!text || page >= 8) return;

    while (*text && x < OLED_WIDTH) {
        draw_char(x, page, *text);
        x += 6;
        text++;
    }
}

/**
 * @brief Draw a character scaled 2x (12x16 pixels, spans 2 pages)
 */
static void draw_char_2x(uint8_t x, uint8_t page, char c)
{
    if (c < 32 || c > 126) c = ' ';
    int idx = c - 32;

    for (int col = 0; col < 6; col++) {
        uint8_t src = font_6x8[idx][col];
        // Stretch each bit vertically: 1 src bit -> 2 dst bits
        uint16_t stretched = 0;
        for (int bit = 0; bit < 8; bit++) {
            if (src & (1 << bit)) {
                stretched |= (3 << (bit * 2));  // 2 bits per original bit
            }
        }
        uint8_t lo = (uint8_t)(stretched & 0xFF);
        uint8_t hi = (uint8_t)((stretched >> 8) & 0xFF);

        // Write 2 columns (horizontal stretch)
        uint8_t dx = x + col * 2;
        if (dx < OLED_WIDTH && page < 8) {
            framebuffer[page * OLED_WIDTH + dx] = lo;
            framebuffer[page * OLED_WIDTH + dx + 1] = lo;
        }
        if (dx < OLED_WIDTH && (page + 1) < 8) {
            framebuffer[(page + 1) * OLED_WIDTH + dx] = hi;
            framebuffer[(page + 1) * OLED_WIDTH + dx + 1] = hi;
        }
    }
}

/**
 * @brief Write text at 2x scale (12x16 per char, spans 2 pages)
 */
static void oled_display_text_2x(uint8_t x, uint8_t page, const char *text)
{
    if (!text || page >= 7) return;

    while (*text && x < OLED_WIDTH) {
        draw_char_2x(x, page, *text);
        x += 12;
        text++;
    }
}

void oled_display_update(void)
{
    if (!oled_dev || !oled_initialized) return;

    // Set column and page address
    oled_send_cmd(SSD1306_CMD_SET_COL_ADDR);
    oled_send_cmd(0);
    oled_send_cmd(127);
    oled_send_cmd(SSD1306_CMD_SET_PAGE_ADDR);
    oled_send_cmd(0);
    oled_send_cmd(7);

    // Send framebuffer in chunks (I2C max transaction size)
    // Each I2C write: 0x40 (data prefix) + up to 128 bytes
    for (int page = 0; page < 8; page++) {
        uint8_t buf[OLED_WIDTH + 1];
        buf[0] = 0x40; // Co=0, D/C=1 (data)
        memcpy(&buf[1], &framebuffer[page * OLED_WIDTH], OLED_WIDTH);
        i2c_master_transmit(oled_dev, buf, OLED_WIDTH + 1, 100);
    }
}

void oled_display_show_sensors(float temp, float hum, float tc_temp)
{
    if (!oled_initialized) return;

    char line[16];

    oled_display_clear();

    // Row 1 (pages 0-1): T: XX.X°C (I2C sensor)
    if (!isnan(temp)) {
        snprintf(line, sizeof(line), "T:%.1fC", temp);
    } else {
        snprintf(line, sizeof(line), "T: --");
    }
    oled_display_text_2x(0, 0, line);

    // Row 2 (pages 2-3): H: XX.X% (I2C sensor)
    if (!isnan(hum)) {
        snprintf(line, sizeof(line), "H:%.1f%%", hum);
    } else {
        snprintf(line, sizeof(line), "H: --");
    }
    oled_display_text_2x(0, 2, line);

    // Row 3 (pages 4-5): TC: XX.X°C (thermocouple)
    if (!isnan(tc_temp)) {
        snprintf(line, sizeof(line), "TC:%.1fC", tc_temp);
    } else {
        snprintf(line, sizeof(line), "TC: --");
    }
    oled_display_text_2x(0, 4, line);

    oled_display_update();
}

void oled_display_show_system(const char *ip_addr, uint32_t uptime_s, uint32_t free_heap,
                               bool lora_joined, uint32_t dev_addr,
                               uint32_t uplink_count, int16_t rssi, float snr)
{
    if (!oled_initialized) return;

    char line[22];

    oled_display_clear();

    oled_display_text(0, 0, "--- SYSTEM ---");

    // IP
    snprintf(line, sizeof(line), "IP:%s", ip_addr ? ip_addr : "N/A");
    oled_display_text(0, 1, line);

    // Uptime
    uint32_t hours = uptime_s / 3600;
    uint32_t mins = (uptime_s % 3600) / 60;
    uint32_t secs = uptime_s % 60;
    snprintf(line, sizeof(line), "Up:%luh%02lum%02lus",
             (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    oled_display_text(0, 2, line);

    // Heap
    snprintf(line, sizeof(line), "Heap:%luKB", (unsigned long)(free_heap / 1024));
    oled_display_text(0, 3, line);

    // LoRaWAN status
    snprintf(line, sizeof(line), "LoRa:%s", lora_joined ? "Joined" : "No Join");
    oled_display_text(0, 4, line);

    if (lora_joined) {
        snprintf(line, sizeof(line), "Addr:%08lX", (unsigned long)dev_addr);
        oled_display_text(0, 5, line);

        snprintf(line, sizeof(line), "Up:%lu R:%ddBm", (unsigned long)uplink_count, rssi);
        oled_display_text(0, 6, line);

        snprintf(line, sizeof(line), "SNR:%.1fdB", snr);
        oled_display_text(0, 7, line);
    }

    oled_display_update();
}

void oled_display_next_page(void)
{
    current_page = (oled_page_t)((current_page + 1) % OLED_PAGE_MAX);
}

oled_page_t oled_display_get_page(void)
{
    return current_page;
}
