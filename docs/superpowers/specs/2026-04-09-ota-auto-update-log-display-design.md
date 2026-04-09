# Design: OTA Auto-Update Log Display

**Date:** 2026-04-09  
**Status:** Approved

## Summary

When the user clicks "Check Now" on the OTA tab, show the auto-updater log messages in real time inside the web interface. Log updates every 2 seconds until the check completes.

## Architecture

### Approach

Dedicated log buffer inside `auto_updater.c`. A private helper `upd_log()` appends key messages to a static `char s_last_run_log[1024]` buffer and also calls `ESP_LOGI`. A `bool s_is_checking` flag tracks whether a check is in progress. Both are exposed via new public API functions. The existing `GET /api/ota/auto-update` endpoint includes the new fields — no new endpoints needed.

## Components

### 1. `main/update/auto_updater.c`

New state:
```c
#define LAST_RUN_LOG_SIZE 1024
static char s_last_run_log[LAST_RUN_LOG_SIZE] = {0};
static bool s_is_checking = false;
```

New private helper:
```c
static void upd_log(const char *fmt, ...);
// Appends formatted line to s_last_run_log (truncates safely at buffer limit)
// Also calls ESP_LOGI(TAG, ...) so serial output is unchanged
```

Changes to `run_check()`:
- Set `s_is_checking = true` and clear `s_last_run_log` at entry
- Replace key `ESP_LOGI` calls with `upd_log()`:
  - "Latest tag for branch..."
  - "New firmware available: X -> Y"
  - "Flashing firmware from: ..."
  - "Firmware size: N bytes"
  - "Firmware flash complete..."
  - "Firmware updated to X, rebooting..."
  - "Already up to date"
  - Error messages
- Set `s_is_checking = false` at all exit points (including early returns)

### 2. `main/update/auto_updater.h`

Two new public functions:
```c
bool        auto_updater_is_checking(void);
const char *auto_updater_get_last_run_log(void);
```

### 3. `main/webserver/handlers/api_ota.c` — `GET /api/ota/auto-update`

Add two fields to the existing JSON response:
```json
{
  "is_checking": false,
  "last_run_log": "Latest tag for branch 'main': main-r7 (N=7)\nNew firmware available: main-r6 -> main-r7\n..."
}
```

No new endpoints. No breaking changes.

### 4. `main/www/tabs/ota.html`

Add a log box below the "Check Now" button, initially hidden:
```html
<div id="au-log-box" class="hidden" style="margin-top:12px;">
  <pre id="au-log-content" style="background:#111;color:#0f0;font-size:0.8em;
       padding:10px;border-radius:4px;max-height:200px;overflow-y:auto;
       white-space:pre-wrap;"></pre>
</div>
```

### 5. `main/www/tabs/ota.js`

Modify `auCheckNow()`:
1. Show `#au-log-box`, clear `#au-log-content`
2. Start a 2s polling interval calling `GET /api/ota/auto-update`
3. On each response, update `#au-log-content` with `last_run_log`
4. Stop polling when `is_checking === false` and log is non-empty

Polling is independent of the existing `refreshOtaStatus()` 3s timer.

## Data Flow

```
[Check Now click]
    → auCheckNow() → POST /api/ota/auto-update {trigger_now: true}
    → show log box, start 2s poll
    → GET /api/ota/auto-update (every 2s)
        ← {is_checking: true/false, last_run_log: "..."}
    → update log box content
    → stop poll when is_checking=false && log non-empty
```

## Error Handling

- Buffer truncation: `upd_log()` stops appending when `LAST_RUN_LOG_SIZE` is reached (no overflow)
- `s_is_checking` is set to `false` at every exit point in `run_check()` including error paths
- If the device reboots mid-check (firmware update), polling fails gracefully — the existing "device rebooting" toast from `auCheckNow()` is not affected

## Constraints

- No thread-safety issues: `run_check()` runs on a single FreeRTOS task; HTTP handlers only read the buffer
- Concurrent access: the HTTP handler may read `s_last_run_log` while `run_check()` is still appending to it. Worst case is a partially-updated log string visible for one poll cycle — acceptable for a log display, corrected on the next 2s poll
- Buffer size of 1024 bytes is sufficient for all key log lines (~6-8 lines of ~80 chars each)
