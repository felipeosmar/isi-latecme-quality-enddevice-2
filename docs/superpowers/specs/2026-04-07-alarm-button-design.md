# Alarm & Button Handler — Design Spec

**Date:** 2026-04-07  
**Branch:** salt_spray_dev  
**Status:** Approved

---

## Overview

Add threshold-based alarm functionality to the ESP32 sensor node. Each measured value (I2C temperature, I2C humidity, thermocouple temperature) gets a configurable low/high threshold. When a threshold is violated, a buzzer siren activates and the OLED shows a dedicated alarm page. A physical button on GPIO25 acknowledges the alarm, navigates OLED pages, and performs factory reset on long press.

---

## New Files

```
main/
├── alarm/
│   ├── alarm_manager.c
│   └── alarm_manager.h
└── interface/
    ├── button_handler.c
    └── button_handler.h
```

---

## Modified Files

| File | Change |
|---|---|
| `config_manager.c/h` | 9 new alarm threshold fields (enabled + low + high per sensor) |
| `oled_display.c/h` | New `oled_display_show_alarm()` function |
| `display_task` (main.c) | Alarm-aware page selection; remove auto-cycle timer |
| `webserver/handlers/api_sensors.c` | Expose alarm thresholds in GET/POST |
| `main/www/tabs/config.html` | New "Alarmes" section in config tab |
| `main.c` | Create alarm_manager and button_handler tasks |
| `CMakeLists.txt` | Add new source files |

---

## Module: `alarm_manager`

### State Machine (per channel)

Each of the 3 sensor channels (temp, humidity, thermocouple) runs an independent instance:

```
IDLE ──(value violates threshold)──▶ ACTIVE
  ▲                                      │
  │ (value returns to normal range)      │ (short button press → acknowledge)
  │                                      ▼
  └──────────────────────────────── ACKNOWLEDGED
```

- **IDLE**: no alarm. Transitions to ACTIVE when `value < low` or `value > high` (and threshold is enabled).
- **ACTIVE**: alarm firing. Siren playing, alarm page shown on OLED. Transitions to ACKNOWLEDGED on button short press.
- **ACKNOWLEDGED**: silenced by user. No siren, no alarm page. Returns to IDLE only when the value re-enters the normal range — allowing a new ACTIVE transition on the next violation.

### Siren Task

`alarm_manager` owns an internal `alarm_siren_task` that runs only while any channel is in ACTIVE state (not ACKNOWLEDGED). Pattern: 3 rapid beeps (100ms ON / 100ms OFF) followed by a 2s pause, looping indefinitely. Uses `buzzer_beep_pattern(3, 100, 100)` + `vTaskDelay(2000ms)`. The task is created on first ACTIVE entry and deleted when all channels leave ACTIVE.

### Priority Display

When multiple channels are simultaneously ACTIVE, `alarm_manager_get_active_info()` returns the highest-priority one. Priority order: thermocouple > temperature > humidity. The OLED label includes a count of additional active alarms if more than one is active (e.g., "+ 1 alarme").

### Public API

```c
esp_err_t  alarm_manager_init(void);

// Called by sensor_task after each read
void       alarm_manager_evaluate(const sensor_data_t *data);

// Called by button_handler on short press
void       alarm_manager_acknowledge(void);

// Queried by display_task and button_handler
bool       alarm_manager_is_active(void);

typedef struct {
    char  label[24];   // e.g. "TEMP ALTA", "UMID BAIXA"
    float value;       // current reading
    float threshold;   // violated limit
    bool  is_high;     // true = above high limit, false = below low limit
    int   extra_count; // number of additional active alarms (0 if only one)
} alarm_info_t;

bool alarm_manager_get_active_info(alarm_info_t *info);
```

---

## Module: `button_handler`

### Hardware

- GPIO: **25**
- Configuration: input with internal pull-up, active LOW (press = GND)
- ISR on falling edge (button pressed)

### Press Classification

A `button_task` (Core 0, Priority 4, Stack 2KB) receives ISR notifications via `xTaskNotifyFromISR` and classifies presses:

| Duration | Action |
|---|---|
| Released before 2s | Short press |
| Still held at 30s | Long press (factory reset) |

50ms software debounce: ISR ignores transitions within 50ms of the last valid event.

### Dispatch Logic

```c
// Short press:
if (alarm_manager_is_active()) {
    alarm_manager_acknowledge();
    // page does NOT change
} else {
    oled_display_next_page();   // cycles sensor ↔ system
}

// Long press (30s reached, button still held):
// 1. Feedback: buzzer_beep_pattern(3, 200, 100)
// 2. config_reset_defaults()
// 3. config_save()
// 4. esp_restart()
```

### Public API

```c
esp_err_t button_handler_init(void);   // configures GPIO, ISR, creates task
```

---

## Configuration

### New Fields in `config_t`

```c
// Per channel: enabled flag + low threshold + high threshold
bool  alarm_temp_enabled;
float alarm_temp_low;
float alarm_temp_high;

bool  alarm_hum_enabled;
float alarm_hum_low;
float alarm_hum_high;

bool  alarm_tc_enabled;
float alarm_tc_low;
float alarm_tc_high;
```

**Factory defaults:** all `enabled = false`, `low = 0.0`, `high = 0.0`.

### Web API

Thresholds are added to the existing `/api/sensors/config` endpoint — no new endpoint needed.

```json
// New fields in GET /api/sensors/config response:
{
  "alarm_temp_enabled": false, "alarm_temp_low": 0.0, "alarm_temp_high": 0.0,
  "alarm_hum_enabled":  false, "alarm_hum_low":  0.0, "alarm_hum_high":  0.0,
  "alarm_tc_enabled":   false, "alarm_tc_low":   0.0, "alarm_tc_high":   0.0
}
```

The web UI gains an "Alarmes" section in the config tab with enable toggles and min/max input fields per sensor channel.

---

## OLED Changes

### Auto-Cycle Removed

The display no longer cycles pages automatically. It boots on `OLED_PAGE_SENSORS` and stays there until the user presses the button. The `page_timer` and auto-cycle logic are removed from `display_task`.

### Alarm Page Layout

```
┌────────────────────────┐
│  *** ALARME ***        │  ← header blinks at 1Hz (invert background)
│                        │
│  TEMP ALTA             │  ← channel label
│  Atual:  67.3°C        │  ← current value
│  Limite: 50.0°C        │  ← violated threshold
│                        │
│  [botao p/ silenciar]  │  ← hint text
└────────────────────────┘
```

If multiple alarms are active: most urgent shown, with "+ N alarme(s)" appended to the label line.

### Updated `display_task` Loop

```c
while (1) {
    if (alarm_manager_is_active()) {
        alarm_info_t info;
        alarm_manager_get_active_info(&info);
        oled_display_show_alarm(&info);   // handles internal 1Hz blink
    } else {
        switch (oled_display_get_page()) {
            case OLED_PAGE_SENSORS: /* show sensors */ break;
            case OLED_PAGE_SYSTEM:  /* show system  */ break;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(500));  // 2Hz refresh for alarm blink
}
```

---

## FreeRTOS Tasks Summary

| Task | Created by | Core | Pri | Stack |
|---|---|---|---|---|
| `button_task` | `button_handler_init()` | 0 | 4 | 2KB |
| `alarm_siren_task` | `alarm_manager` (on demand) | 0 | 3 | 2KB |

`alarm_manager_init()` and `button_handler_init()` are called from `app_main` before the FreeRTOS task spawn block.

---

## Sequence: Alarm Trigger → Acknowledge → Re-arm

```
sensor_task reads data
    → alarm_manager_evaluate(&data)
        → channel transitions IDLE → ACTIVE
        → alarm_siren_task created
display_task (500ms loop)
    → alarm_manager_is_active() == true
    → oled_display_show_alarm(&info)  [blinks]

[user presses button]
button_task detects short press
    → alarm_manager_is_active() == true
    → alarm_manager_acknowledge()
        → channel transitions ACTIVE → ACKNOWLEDGED
        → alarm_siren_task deleted (buzzer stops)
display_task
    → alarm_manager_is_active() == false
    → resumes normal page display

[value returns to normal range]
sensor_task → alarm_manager_evaluate(&data)
    → channel transitions ACKNOWLEDGED → IDLE
    [alarm can fire again on next violation]
```
