# Clock Sync (TS003) — Design Spec

**Date:** 2026-04-07
**Branch:** salt_spray

## Overview

Implement LoRa Alliance TS003 Application Layer Clock Synchronization on the ESP32 firmware. After joining the LoRaWAN network, the device requests the current time from ChirpStack via a dedicated uplink on FPort 202. The server responds with a time correction that is applied via `settimeofday()`. Re-sync happens once per day automatically.

## Use Cases

- **Logs:** ESP-IDF log timestamps show real date/time after sync (via `CONFIG_LOG_TIMESTAMP_SOURCE_SYSTEM`)
- **Sensor payload:** Caller code can use `time(NULL)` to timestamp readings
- **Uplink alignment:** After sync, uplinks are scheduled to the next clock-aligned boundary (e.g., hourly at :00:00)
- **OLED:** System page shows current time; displays `--:--:--` when not synced
- **Daily auto-sync:** A FreeRTOS task re-requests sync every 24 hours

## Architecture

```
clock_sync.c  (app-layer: TS003)
  └── lorawan_register_downlink_callback(202, on_apptime_ans)
  └── lorawan_send(AppTimeReq, FPort 202)

lorawan_handler.cpp  (transport layer)
  └── after sendReceive(): dispatch downlink by FPort to registered callback
```

No persistence — time is lost on reboot and re-synced after joining.

## Protocol: TS003

**AppTimeReq** (uplink, FPort 202, 5 bytes):

| Bytes | Field       | Type     | Description                              |
|-------|-------------|----------|------------------------------------------|
| 0–3   | DeviceTime  | uint32_t | Seconds since GPS epoch (6 Jan 1980), LE |
| 4     | Param       | uint8_t  | bit0=1 (AnsRequired)                     |

Before the first sync, `DeviceTime = uptime_seconds` (best-effort, offset from GPS epoch 0). The server computes the correction using its own real time regardless.

**AppTimeAns** (downlink, FPort 202, 5 bytes):

| Bytes | Field          | Type    | Description                        |
|-------|----------------|---------|------------------------------------|
| 0–3   | TimeCorrection | int32_t | Correction in seconds (signed), LE |
| 4     | Param          | uint8_t | bits[3:0] = TokenReq               |

**Epoch conversion:**

```c
#define GPS_TO_UNIX_OFFSET  315964800UL  // seconds between 1970-01-01 and 1980-01-06

unix_time = (device_time_sent + time_correction) + GPS_TO_UNIX_OFFSET;
settimeofday(&(struct timeval){ .tv_sec = unix_time }, NULL);
```

Leap seconds are ignored (18s error is acceptable for logging and display purposes).

## Components

### New: `main/clock_sync/clock_sync.h`

```c
esp_err_t clock_sync_init(void);
esp_err_t clock_sync_request(void);
bool      clock_sync_is_synced(void);
time_t    clock_sync_get_time(void);   // returns 0 if not synced
void      clock_sync_task(void *param);
```

### New: `main/clock_sync/clock_sync.c`

- `clock_sync_init()`: registers downlink callback for FPort 202 via `lorawan_register_downlink_callback()`
- `clock_sync_request()`: reads uptime, builds 5-byte AppTimeReq, calls `lorawan_send(FPort 202)`; the AppTimeAns arrives via callback synchronously before `lorawan_send()` returns
- Callback `on_apptime_ans()`: parses TimeCorrection, validates range (reject if `|correction| > 365 * 24 * 3600`), calls `settimeofday()`, sets `s_synced = true`
- `clock_sync_task()`: waits for `lorawan_is_joined()`, calls `clock_sync_request()`, then loops with `vTaskDelay(24h)`

### Modified: `main/lorawan/lorawan_handler.h`

Add:

```c
typedef void (*lorawan_downlink_cb_t)(uint8_t port, const uint8_t *data, size_t len);
esp_err_t lorawan_register_downlink_callback(uint8_t port, lorawan_downlink_cb_t cb);
```

### Modified: `main/lorawan/lorawan_handler.cpp`

- Add static table of up to 8 `{port, cb}` entries
- In `lorawan_send()`: pass `LoRaWANEvent_t eventDown` to `sendReceive()` to capture downlink FPort
- After releasing the mutex, if a downlink was received, look up and call the matching callback
- Callback is called **outside** the mutex to avoid deadlock

### Modified: `main/main.c`

- Include `clock_sync.h`
- Call `clock_sync_init()` in `app_main()` before spawning tasks (only registers a static callback — does not depend on radio being initialized)
- Spawn `clock_sync_task` on Core 0, Priority 3, Stack 3072
- Modify `uplink_task`: if synced, delay to next aligned boundary; otherwise use current simple interval

### Modified: `main/display/oled_display.c/h`

- `oled_display_show_system()`: add a time line formatted with `strftime()`; show `"--:--:--"` if `!clock_sync_is_synced()`

### Modified: `main/CMakeLists.txt`

Add `clock_sync/clock_sync.c` to `SRCS`.

### Modified: `sdkconfig.defaults`

Add:

```
CONFIG_LOG_TIMESTAMP_SOURCE_SYSTEM=y
```

## Data Flow

```
[clock_sync_task]
    → lorawan_is_joined() == true
    → clock_sync_request()
        → build AppTimeReq (5 bytes, FPort 202)
        → lorawan_send(data, 5, 202, false)
            → node->sendReceive(..., &eventDown)
            → downlink received (FPort 202, 5 bytes)
            → release mutex
            → dispatch: on_apptime_ans(202, data, 5)
                → parse TimeCorrection
                → validate |correction| < 1 year
                → unix_time = device_time + correction + GPS_TO_UNIX_OFFSET
                → settimeofday(unix_time)
                → s_synced = true
        → lorawan_send() returns
    → clock_sync_request() returns ESP_OK
    → vTaskDelay(24h)
    → repeat
```

## Error Handling

| Scenario | Behavior |
|----------|----------|
| No downlink (server timeout) | `lorawan_send()` returns, callback not called, `s_synced` remains false, retry at next daily cycle |
| `\|TimeCorrection\|` > 1 year | Correction rejected, log warning, `s_synced` not set |
| Not joined at boot | `clock_sync_task` blocks in `lorawan_is_joined()` loop — no crash, no sync until joined |
| `lorawan_send()` fails | `clock_sync_request()` returns error, logs it, task retries after 24h |

## Uplink Alignment

```c
// In uplink_task (main.c):
if (clock_sync_is_synced()) {
    time_t now = time(NULL);
    time_t next = ((now / interval_s) + 1) * interval_s;
    uint32_t delay_ms = (uint32_t)((next - now) * 1000);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
} else {
    vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));  // current behavior
}
```

## Task Parameters

| Task            | Core | Priority | Stack |
|-----------------|------|----------|-------|
| clock_sync_task | 0    | 3        | 3072  |

## Files Summary

| File | Action |
|------|--------|
| `main/clock_sync/clock_sync.h` | Create |
| `main/clock_sync/clock_sync.c` | Create |
| `main/lorawan/lorawan_handler.h` | Modify |
| `main/lorawan/lorawan_handler.cpp` | Modify |
| `main/main.c` | Modify |
| `main/display/oled_display.c` | Modify |
| `main/display/oled_display.h` | Modify |
| `main/CMakeLists.txt` | Modify |
| `sdkconfig.defaults` | Modify |
