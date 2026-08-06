# FUOTA via LoRaWAN + ChirpStack — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Firmware Update Over The Air (FUOTA) via LoRaWAN Class C multicast against ChirpStack v4, with delta-OTA to minimize transfer time. Applies only to 4MB SKUs (`jvtech_4mb_standard`, `jvtech_4mb_thermocouple`).

**Architecture:** Three LoRa Alliance application packages (TS004 Fragmented Data Block Transport, TS005 Remote Multicast Setup, TS006 Firmware Management) registered as RadioLib `addAppPackage` callbacks on FPorts 201/200/203. A `fuota_manager` state machine orchestrates: (1) TS005 receives multicast session setup, (2) `lorawan_handler` switches to Class C on the multicast freq/DR, (3) TS004 receives fragments and writes them incrementally to the inactive OTA partition, (4) esp_delta_ota applies patch on top of active slot contents during reception, (5) TS006 validates hash + reboots via `esp_ota_set_boot_partition`. RX2 unicast raised to DR10 (SF10/500kHz, 222 B) via ChirpStack Device Profile. Legacy WiFi/HTTPS OTA path (`auto_updater.c`, `api_ota.c`) remains untouched and operational in parallel.

**Tech Stack:**
- ESP-IDF v5.5.3, FreeRTOS
- RadioLib 7.5.0 (C++ via `extern "C"` wrapper)
- ChirpStack v4 (network server + chirpstack-fuota-server)
- `espressif/esp_delta_ota` v1.1.4 (detools + heatshrink)
- Unity (ESP-IDF native) for host-logic tests
- `detools` Python package for server-side patch generation
- LoRa Alliance specs: TS003-2.0.0, TS004-2.0.0, TS005-2.0.0, TS006-1.0.0, RP002-1.0.3 (AU915)

---

## Prerequisites

Before starting execution:

1. **Create a worktree** via `superpowers:using-git-worktrees` — this is a 5–6 week effort and must not block `main_dev`.
2. **ChirpStack test environment** with:
   - chirpstack-fuota-server deployed and reachable
   - A test device provisioned (DevEUI/JoinEUI/AppKey in `config.json`)
   - Device Profile configured for AU915 (sub-band to match hardware)
3. **Two physical JVTECH v1.2 boards (4MB)** — one for development, one for multicast reception tests.
4. **Python 3.10+** with `detools>=0.49.0` on the build host.
5. **ESP-IDF v5.5.3 environment sourced**: `. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh`.

## Test Strategy

The codebase has **no existing unit-test infrastructure** (per `CLAUDE.md`). This plan introduces:

- **Host-runnable Unity tests** in `test/` (ESP-IDF's `idf.py -T all build-tests` harness, using QEMU or PC-host target where possible) for pure logic:
  - TS004 fragment reassembly with forward error correction
  - Delta-OTA patch header parsing
  - Version/hash comparison
- **Hardware-in-the-loop (HIL) test scripts** in `tools/hil/` — Python scripts that drive a real ChirpStack server to produce a specific scenario, while `idf.py monitor` captures UART output for verification.
- **Manual acceptance checklists** at phase boundaries (section "Acceptance" in each phase).

For steps that modify embedded code but produce no directly testable artifact (plumbing, build-system changes), the verification step is `./build.sh build jvtech_4mb_standard` completing with zero warnings — which is this project's equivalent of "compile-test".

## Commit Cadence

Commit after each task (not each step) unless a task explicitly says otherwise. Commit message prefix conventions already in use: `feat:`, `fix:`, `refactor:`, `test:`, `docs:`, `build:`. Use `feat(fuota):` scope for all new work to keep the log filterable.

---

## File Structure

### New files

| File | Responsibility |
|---|---|
| `main/fuota/fuota_manager.c` | State machine orchestrator; owns FUOTA session lifecycle |
| `main/fuota/fuota_manager.h` | Public API: `fuota_init`, `fuota_task`, status getter |
| `main/fuota/ts004_fragmentation.c` | TS004 Fragmented Data Block Transport: FragSessionSetup/Status/Data/Delete handlers, FEC reconstruction |
| `main/fuota/ts004_fragmentation.h` | TS004 public API: session begin/end, fragment ingest, completion callback |
| `main/fuota/ts005_multicast.c` | TS005 Remote Multicast Setup: McGroupSetup/Delete, McClassCSessionReq/Ans handlers |
| `main/fuota/ts005_multicast.h` | TS005 public API: group registration, session start/stop callbacks |
| `main/fuota/ts006_fmp.c` | TS006 Firmware Management Protocol: PackageVersion, DevVersion, DevReboot, DevUpgradeImage |
| `main/fuota/ts006_fmp.h` | TS006 public API: register package, report version, install handler |
| `main/fuota/fuota_flash.c` | Incremental writer to inactive OTA partition; tracks received-fragment bitmap in NVS |
| `main/fuota/fuota_flash.h` | Flash writer API: `begin`, `write_fragment(idx, data, len)`, `has_fragment(idx)`, `finalize`, `reset` |
| `main/fuota/delta_ota_wrapper.c` | Wraps `esp_delta_ota` streaming API; translates TS004 output into patch input |
| `main/fuota/delta_ota_wrapper.h` | Delta API: `begin`, `feed(data, len)`, `finalize` → sets boot partition |
| `main/fuota/CMakeLists.txt` | Component-local CMake stanza (optional, if kept as subcomponent) |
| `test/test_ts004_reassembly.c` | Unity: feeds known fragments + parity to TS004, asserts reassembled buffer matches fixture |
| `test/test_fuota_flash.c` | Unity on target: writes fragments to a scratch partition, verifies bitmap survives reboot |
| `test/CMakeLists.txt` | Test component registration |
| `tools/fuota/generate_patch.py` | Wraps `detools` to produce `patch.bin` from (old.bin, new.bin); emits sidecar manifest |
| `tools/fuota/chirpstack_upload.py` | Uploads image/patch + creates FUOTA deployment via chirpstack-fuota-server gRPC |
| `tools/hil/hil_test_fuota.py` | End-to-end HIL test driver |
| `docs/FUOTA.md` | Operator documentation: how to release, how to deploy a FUOTA campaign |

### Modified files

| File | Change |
|---|---|
| `main/lorawan/lorawan_handler.cpp` | Add `lorawan_switch_class`, `lorawan_start_multicast_session`, `lorawan_stop_multicast_session`; relax 51-byte uplink cap to region max (`node->getMaxPayloadLen()`); expose `lorawan_run_class_c_rx` for Class C windows |
| `main/lorawan/lorawan_handler.h` | Declare new APIs + `lorawan_class_t` enum |
| `main/main.c` | Init `fuota_init()` (4MB builds only), spawn `fuota_task` on Core 0 |
| `main/idf_component.yml` | Add `espressif/esp_delta_ota: "^1.1.4"` |
| `main/CMakeLists.txt` | Add `fuota/*.c` to `srcs` (guarded by `CONFIG_FUOTA_ENABLED`), add `fuota` to `INCLUDE_DIRS`, add `esp_delta_ota` to `REQUIRES` |
| `main/Kconfig.projbuild` | Add `CONFIG_FUOTA_ENABLED` bool (default y on 4MB, n on 2MB SKUs) |
| `main/config/config_manager.c` | Add `fuota.enabled`, `fuota.firmware_version` (string), `fuota.last_session_id` to JSON load/save |
| `main/config/config_manager.h` | Declare `config_get_fuota_enabled`, `config_get_firmware_version`, `config_set_firmware_version` |
| `config.json` | Add `"fuota": { "enabled": true, "firmware_version": "" }` block |
| `main/webserver/handlers/api_system.c` | Extend `/api/system/status` response with `fuota` object: `{ state, session_id, progress_pct, last_error }` |
| `sdkconfig.defaults` | Set `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` (verify already set; enables rollback if new image fails to boot) |
| `main/sku/sku_defs.h` | Add `SKU_HAS_FUOTA` macro (1 for 4MB, 0 for 2MB) |
| `BUILDING.md` | New section "FUOTA deployment" with `tools/fuota/*.py` usage |
| `CLAUDE.md` | Add FUOTA architecture section (tasks, FPorts, state machine) |

### Unchanged but worth re-reading during execution

- `main/clock_sync/clock_sync.c` — reference implementation of a TS-style package (TS003). Mirror its patterns: constants in the `.c` file, callback registration in the task, deferred until joined.
- `partitions.csv` — already has `ota_0` (1.625 MB) and `ota_1` (1.625 MB) with 4MB flash. No change needed.
- `main/update/auto_updater.c` — must continue to work unmodified. Delta-OTA wrapper mirrors its `esp_ota_*` call pattern but is invoked from FUOTA, not HTTP.

---

## LoRa Alliance FPort Allocation (reference)

| FPort | Package | Spec | CID range |
|---|---|---|---|
| 200 | Remote Multicast Setup | TS005 | 0x00–0x04 |
| 201 | Fragmented Data Block Transport | TS004 | 0x00–0x08 |
| 202 | Application Layer Clock Sync | TS003 | 0x00–0x01 (**in use**) |
| 203 | Firmware Management Protocol | TS006 | 0x00–0x05 |

RadioLib `PackageCb_t` delivers the full downlink payload with CID as byte 0 (matches how `clock_sync.c` on_apptime_ans handles it).

---

# Phase 0 — Preparation (≈ 3 days)

## Task 0.1: Add `esp_delta_ota` dependency and verify build

**Files:**
- Modify: `main/idf_component.yml`

- [ ] **Step 1: Add dependency**

Edit `main/idf_component.yml` to:

```yaml
dependencies:
  joltwallet/littlefs: "*"
  jgromes/radiolib: "^7.5.0"
  espressif/led_strip: "^3.0.0"
  espressif/esp_delta_ota: "^1.1.4"
```

- [ ] **Step 2: Verify the managed component downloads**

Run: `. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh && ./build.sh build jvtech_4mb_standard`
Expected: build succeeds; `managed_components/espressif__esp_delta_ota/` directory exists.

- [ ] **Step 3: Commit**

```bash
git add main/idf_component.yml main/idf_component.yml.lock
git commit -m "build(fuota): add esp_delta_ota v1.1.4 dependency"
```

## Task 0.2: Add `CONFIG_FUOTA_ENABLED` Kconfig

**Files:**
- Modify: `main/Kconfig.projbuild` (create if missing; search for existing project Kconfig first)
- Modify: `main/sku/sku_defs.h`

- [ ] **Step 1: Locate project Kconfig**

Run: `find main -name 'Kconfig*' | head -20`
If a project Kconfig already exists (likely `main/Kconfig.projbuild` based on existing `CONFIG_THERMOCOUPLE_ENABLED`, `CONFIG_OLED_ENABLED`, `CONFIG_WIFI_ENABLED`), modify it. Otherwise create `main/Kconfig.projbuild`.

- [ ] **Step 2: Add the config entry**

Append to the existing SKU-feature block in `main/Kconfig.projbuild`:

```
config FUOTA_ENABLED
    bool "Enable LoRaWAN FUOTA (requires OTA A/B partitions)"
    default n
    help
        Enable Firmware Update Over The Air via LoRaWAN multicast
        (TS004/TS005/TS006 + esp_delta_ota). Requires dual OTA
        partitions — only available on 4MB SKUs.
```

- [ ] **Step 3: Set default per SKU in `main/sku/sku_defs.h`**

Read `main/sku/sku_defs.h` and add:

```c
#if defined(SKU_JVTECH_4MB_STANDARD) || defined(SKU_JVTECH_4MB_THERMOCOUPLE)
  #define SKU_HAS_FUOTA 1
#else
  #define SKU_HAS_FUOTA 0
#endif
```

- [ ] **Step 4: Wire SKU default into sdkconfig.defaults**

Locate the per-SKU `sdkconfig.defaults.*` files (there is one per SKU, per `build.sh`). For each 4MB SKU defaults file, add: `CONFIG_FUOTA_ENABLED=y`. For 2MB SKU defaults, add: `CONFIG_FUOTA_ENABLED=n`.

- [ ] **Step 5: Verify build for all 4 SKUs**

Run: `./build.sh skus` to list them, then for each: `./build.sh build <sku>`. All must succeed.

- [ ] **Step 6: Commit**

```bash
git add main/Kconfig.projbuild main/sku/sku_defs.h sdkconfig.defaults.*
git commit -m "build(fuota): add CONFIG_FUOTA_ENABLED kconfig, default on 4MB SKUs"
```

## Task 0.3: Add FUOTA config fields

**Files:**
- Modify: `config.json`
- Modify: `main/config/config_manager.c`
- Modify: `main/config/config_manager.h`

- [ ] **Step 1: Extend `config.json` default template**

Add before the closing `}`:

```json
    "fuota": {
        "enabled": true,
        "firmware_version": ""
    }
```

- [ ] **Step 2: Read existing config_manager.c to locate load/save internals**

Run: `grep -n '"lorawan"' main/config/config_manager.c | head`
Mirror the pattern used for the `lorawan` block to add a `fuota` block parsing/serializing.

- [ ] **Step 3: Add struct fields and defaults**

In `config_manager.c`, extend the `config_t` struct (or equivalent) with:

```c
struct {
    bool enabled;
    char firmware_version[32];  // semver string, e.g. "1.2.3"
} fuota;
```

In `config_reset_defaults()`:

```c
cfg.fuota.enabled = true;
strncpy(cfg.fuota.firmware_version, "", sizeof(cfg.fuota.firmware_version));
```

- [ ] **Step 4: Add load/save JSON code**

In the JSON load routine, add after the `lorawan` block:

```c
cJSON *fuota_obj = cJSON_GetObjectItem(root, "fuota");
if (fuota_obj) {
    cJSON *en = cJSON_GetObjectItem(fuota_obj, "enabled");
    if (cJSON_IsBool(en)) cfg.fuota.enabled = cJSON_IsTrue(en);
    cJSON *ver = cJSON_GetObjectItem(fuota_obj, "firmware_version");
    if (cJSON_IsString(ver)) strncpy(cfg.fuota.firmware_version, ver->valuestring,
                                     sizeof(cfg.fuota.firmware_version) - 1);
}
```

Symmetric serialization in the save routine.

- [ ] **Step 5: Add public getters/setters**

In `main/config/config_manager.h`:

```c
bool config_get_fuota_enabled(void);
const char *config_get_firmware_version(void);
esp_err_t config_set_firmware_version(const char *version);  // persists to flash
```

Implement in `config_manager.c` following the existing getter/setter pattern.

- [ ] **Step 6: Populate `firmware_version` on first boot**

If `cfg.fuota.firmware_version` is empty after load, set it to the compiled `PROJECT_VER` (ESP-IDF built-in macro, from `idf.py` project metadata) and persist:

```c
if (cfg.fuota.firmware_version[0] == '\0') {
    strncpy(cfg.fuota.firmware_version, esp_app_get_description()->version,
            sizeof(cfg.fuota.firmware_version) - 1);
    // Note: don't call save here — let the first write trigger it
}
```

- [ ] **Step 7: Build and commit**

```bash
./build.sh build jvtech_4mb_standard
git add config.json main/config/config_manager.c main/config/config_manager.h
git commit -m "feat(fuota): add config fields (enabled, firmware_version)"
```

## Task 0.4: Create `main/fuota/` component skeleton

**Files:**
- Create: `main/fuota/fuota_manager.h`
- Create: `main/fuota/fuota_manager.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/main.c`

- [ ] **Step 1: Create `fuota_manager.h`**

```c
/**
 * @file fuota_manager.h
 * @brief LoRaWAN FUOTA orchestrator (TS004/TS005/TS006 + esp_delta_ota)
 */
#ifndef FUOTA_MANAGER_H
#define FUOTA_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    FUOTA_STATE_IDLE = 0,
    FUOTA_STATE_MC_SETUP,       // McGroupSetupReq received, awaiting McClassCSessionReq
    FUOTA_STATE_FRAG_SETUP,     // FragSessionSetupReq received
    FUOTA_STATE_DOWNLOADING,    // Receiving fragments in Class C
    FUOTA_STATE_DOWNLOAD_DONE,  // All fragments received (or FEC-reconstructed)
    FUOTA_STATE_APPLYING,       // Delta patch being applied to inactive slot
    FUOTA_STATE_VERIFIED,       // Hash OK, awaiting DevUpgradeImageReq
    FUOTA_STATE_REBOOTING,      // esp_ota_set_boot_partition called; reboot pending
    FUOTA_STATE_ERROR,
} fuota_state_t;

typedef struct {
    fuota_state_t state;
    uint32_t session_id;
    uint16_t fragments_total;
    uint16_t fragments_received;
    uint8_t  progress_pct;        // 0-100
    char     last_error[64];
} fuota_status_t;

/**
 * @brief Initialize FUOTA subsystem. Call after lorawan_init() and config_init().
 *        Idempotent; returns ESP_OK if already initialized.
 */
esp_err_t fuota_init(void);

/**
 * @brief FreeRTOS task: registers TS004/005/006 packages after join, then idles
 *        as a coordinator between the three callbacks. Create with:
 *        xTaskCreatePinnedToCore(fuota_task, "fuota", 8192, NULL, 4, NULL, 0)
 */
void fuota_task(void *param);

/**
 * @brief Get a copy of the current FUOTA status (thread-safe).
 */
esp_err_t fuota_get_status(fuota_status_t *out);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Create `fuota_manager.c` stub**

```c
/**
 * @file fuota_manager.c
 * @brief FUOTA state machine — stub; real logic wired in Phases 1-5
 */
#include "fuota_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lorawan_handler.h"
#include "config_manager.h"

static const char *TAG = "FUOTA";
static fuota_status_t s_status;
static SemaphoreHandle_t s_mutex;

esp_err_t fuota_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = FUOTA_STATE_IDLE;
    ESP_LOGI(TAG, "FUOTA manager initialized (fw version: %s)",
             config_get_firmware_version());
    return ESP_OK;
}

esp_err_t fuota_get_status(fuota_status_t *out)
{
    if (!out || !s_mutex) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;
    memcpy(out, &s_status, sizeof(*out));
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void fuota_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "FUOTA task started (awaiting LoRaWAN join)");
    while (!lorawan_is_joined()) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    ESP_LOGI(TAG, "FUOTA task: joined. Package handlers will be registered in Phase 1-3.");
    // Phases 1-3 will register TS005/TS004/TS006 here.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
```

- [ ] **Step 3: Register in `main/CMakeLists.txt`**

Add inside an `if(CONFIG_FUOTA_ENABLED)` block (mirror the existing `CONFIG_WIFI_ENABLED` block pattern):

```cmake
if(CONFIG_FUOTA_ENABLED)
    list(APPEND srcs
        "fuota/fuota_manager.c"
    )
endif()
```

Add `"fuota"` to `INCLUDE_DIRS`. Add `esp_delta_ota` to `requires` when appropriate (deferred to Task 4.2 since the wrapper is built later).

- [ ] **Step 4: Hook into `main.c`**

Read the existing block at `main.c:347-355` (auto-updater init). Add below it:

```c
#if CONFIG_FUOTA_ENABLED
    if (fuota_init() == ESP_OK) {
        xTaskCreatePinnedToCore(fuota_task, "fuota", 8192, NULL, 4, NULL, 0);
    } else {
        ESP_LOGW(TAG, "FUOTA init failed");
    }
#endif
```

Include `fuota_manager.h` inside an existing `#if CONFIG_WIFI_ENABLED` block or under a new `#if CONFIG_FUOTA_ENABLED` guard at the top of `main.c`.

- [ ] **Step 5: Build all SKUs, verify 2MB SKUs still build unchanged**

```bash
for sku in jvtech_4mb_standard jvtech_4mb_thermocouple jvtech_2mb_standard jvtech_2mb_headless; do
    ./build.sh build $sku || { echo "FAIL: $sku"; exit 1; }
done
```

Expected: all four succeed. The 4MB builds should show `FUOTA` log lines on boot; 2MB builds should not.

- [ ] **Step 6: Commit**

```bash
git add main/fuota/ main/CMakeLists.txt main/main.c
git commit -m "feat(fuota): add fuota_manager skeleton + task wiring"
```

## Task 0.5: Configure ChirpStack Device Profile for RX2 DR10 (docs only)

**Files:**
- Create: `docs/FUOTA.md` (initial section)

- [ ] **Step 1: Document ChirpStack config steps**

Create `docs/FUOTA.md` with this section (leave other sections as `TBD` stubs to be filled in later phases — note: this is the ONLY time the plan allows a TBD, because later phases explicitly write those sections):

```markdown
# FUOTA Setup Guide

## 1. ChirpStack Device Profile (AU915)

In the ChirpStack web UI:

1. Device Profiles → select profile used by the test devices → **RX2 data rate** = `10`
2. Save. ChirpStack will issue `RXParamSetupReq` on the next downlink; the device's RadioLib stack auto-acks and switches RX2 to DR10 (SF10/500 kHz, 222 B max app payload).

Verification from device UART: after the next uplink, the `LORAWAN` log should show a received MAC command. RadioLib doesn't log this directly — instead verify by deploying a FUOTA session (Phase 3+) and confirming the device receives non-fragmented downlinks larger than 53 bytes.

## 2. chirpstack-fuota-server

Clone and deploy per upstream README: <https://github.com/chirpstack/chirpstack-fuota-server>
Configure it to point at the same ChirpStack instance (application server API endpoint + API key).

Sections below will be populated by Phases 1–5 of the implementation plan.

## 3. Running a FUOTA Campaign — TBD (filled in Phase 5)
## 4. Generating Delta Patches — TBD (filled in Phase 4)
## 5. Monitoring and Rollback — TBD (filled in Phase 5)
```

- [ ] **Step 2: Commit**

```bash
git add docs/FUOTA.md
git commit -m "docs(fuota): initial setup guide with ChirpStack Device Profile steps"
```

### Phase 0 Acceptance

- [ ] All 4 SKUs build.
- [ ] On a 4MB device boot, UART logs include `FUOTA manager initialized (fw version: <semver>)` and `FUOTA task started`.
- [ ] `config get fuota` via API (or read the userdata partition JSON) returns `enabled: true`.
- [ ] `docs/FUOTA.md` ChirpStack section reviewed and applied in the test environment.

---

# Phase 1 — Class C Switching + TS005 Multicast Setup (≈ 1 week)

## Task 1.1: Expose dynamic Class switching in `lorawan_handler`

**Files:**
- Modify: `main/lorawan/lorawan_handler.cpp`
- Modify: `main/lorawan/lorawan_handler.h`

- [ ] **Step 1: Add `lorawan_class_t` enum to header**

Append to `lorawan_handler.h` before the closing `#endif`:

```c
typedef enum {
    LORAWAN_CLASS_A = 'A',
    LORAWAN_CLASS_C = 'C',
} lorawan_class_t;

/**
 * @brief Switch device class at runtime.
 *
 * Switching to Class C opens a continuous RX2 window (radio stays in receive
 * between uplinks). High power consumption — use only during FUOTA sessions.
 * Switching back to Class A restores normal RX1/RX2 after each uplink.
 *
 * Must be called from a task other than lorawan_task (this function acquires
 * lorawan_mutex internally).
 *
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_STATE if not joined
 *         ESP_ERR_TIMEOUT if the lorawan mutex is held longer than 5s
 */
esp_err_t lorawan_switch_class(lorawan_class_t cls);

/**
 * @brief Start a multicast session for FUOTA (TS005).
 *
 * Configures RadioLib with a multicast group's address/keys and sets the
 * downlink data rate and frequency. After this call, switch to Class C
 * with lorawan_switch_class(LORAWAN_CLASS_C) to begin receiving.
 *
 * @param mc_addr   Multicast DevAddr (4 bytes, from McGroupSetupReq)
 * @param mc_app_skey  Multicast AppSKey (16 bytes)
 * @param mc_nwk_skey  Multicast NwkSKey (16 bytes)
 * @param mc_fcnt_min  Minimum acceptable FCnt
 * @param mc_fcnt_max  Maximum acceptable FCnt (session end)
 * @param mc_freq_hz   Downlink frequency in Hz
 * @param mc_dr        Downlink data rate (e.g. 10 for AU915 DR10)
 */
esp_err_t lorawan_start_multicast_session(uint32_t mc_addr,
                                          const uint8_t *mc_app_skey,
                                          const uint8_t *mc_nwk_skey,
                                          uint32_t mc_fcnt_min,
                                          uint32_t mc_fcnt_max,
                                          uint32_t mc_freq_hz,
                                          uint8_t mc_dr);

esp_err_t lorawan_stop_multicast_session(void);

/**
 * @brief Poll the radio for any received multicast downlink.
 *
 * Call periodically from the FUOTA task while in Class C. Dispatches downlinks
 * to registered app packages exactly like the Class A path in lorawan_send.
 *
 * @param timeout_ms Max time to block waiting for a downlink
 * @return ESP_OK if a downlink was delivered, ESP_ERR_TIMEOUT otherwise
 */
esp_err_t lorawan_run_class_c_rx(uint32_t timeout_ms);
```

- [ ] **Step 2: Implement `lorawan_switch_class` in `.cpp`**

Add after `lorawan_force_rejoin` in `lorawan_handler.cpp`:

```cpp
extern "C" esp_err_t lorawan_switch_class(lorawan_class_t cls)
{
    if (!initialized || !node || !stats.joined) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(lorawan_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t c = (cls == LORAWAN_CLASS_C) ? RADIOLIB_LORAWAN_CLASS_C
                                         : RADIOLIB_LORAWAN_CLASS_A;
    int state = node->setDeviceClass(c);
    xSemaphoreGive(lorawan_mutex);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "setDeviceClass failed: %d", state);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Switched to Class %c", (char)cls);
    return ESP_OK;
}
```

**Verification reading:** Before committing, run
`grep -rn "setDeviceClass\|CLASS_C\|startMulticastSession" managed_components/jgromes__radiolib/src/protocols/LoRaWAN/`
to confirm the exact API names. If RadioLib 7.5.0 uses different names (e.g. `setClass`), adapt the wrapper; do not invent APIs.

- [ ] **Step 3: Implement multicast session start/stop**

```cpp
extern "C" esp_err_t lorawan_start_multicast_session(uint32_t mc_addr,
                                                     const uint8_t *mc_app_skey,
                                                     const uint8_t *mc_nwk_skey,
                                                     uint32_t mc_fcnt_min,
                                                     uint32_t mc_fcnt_max,
                                                     uint32_t mc_freq_hz,
                                                     uint8_t mc_dr)
{
    if (!initialized || !node || !stats.joined) return ESP_ERR_INVALID_STATE;
    if (!mc_app_skey || !mc_nwk_skey) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(lorawan_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    // Check the actual RadioLib 7.5.0 signature before committing. Typical:
    // int16_t startMulticastSession(uint8_t cls, uint32_t addr,
    //                               uint8_t* appSKey, uint8_t* nwkSKey,
    //                               uint32_t fcntMin, uint32_t fcntMax,
    //                               uint32_t freq, uint8_t dr);
    int state = node->startMulticastSession(RADIOLIB_LORAWAN_CLASS_C,
                                            mc_addr,
                                            (uint8_t *)mc_app_skey,
                                            (uint8_t *)mc_nwk_skey,
                                            mc_fcnt_min, mc_fcnt_max,
                                            mc_freq_hz, mc_dr);
    xSemaphoreGive(lorawan_mutex);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "startMulticastSession failed: %d", state);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Multicast session started (addr=0x%08lX freq=%lu Hz DR%u)",
             (unsigned long)mc_addr, (unsigned long)mc_freq_hz, mc_dr);
    return ESP_OK;
}

extern "C" esp_err_t lorawan_stop_multicast_session(void)
{
    if (!initialized || !node) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(lorawan_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    node->stopMulticastSession();  // verify name in RadioLib 7.5.0
    xSemaphoreGive(lorawan_mutex);
    ESP_LOGI(TAG, "Multicast session stopped");
    return ESP_OK;
}
```

- [ ] **Step 4: Implement `lorawan_run_class_c_rx`**

```cpp
extern "C" esp_err_t lorawan_run_class_c_rx(uint32_t timeout_ms)
{
    if (!initialized || !node) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(lorawan_mutex, pdMS_TO_TICKS(timeout_ms + 100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    // RadioLib 7.5.0 exposes either node->receiveC(...) or a poll via node->getDownlink()
    // Confirm the exact call; typical pattern:
    uint8_t buf[256];
    size_t  len = sizeof(buf);
    int state = node->receiveC(buf, &len, timeout_ms);  // verify in RadioLib
    xSemaphoreGive(lorawan_mutex);
    if (state == RADIOLIB_ERR_RX_TIMEOUT) return ESP_ERR_TIMEOUT;
    if (state < 0) {
        ESP_LOGW(TAG, "Class C rx error: %d", state);
        return ESP_FAIL;
    }
    stats.downlink_count++;
    // RadioLib internally dispatches to registered app packages — nothing else to do.
    return ESP_OK;
}
```

*If `node->receiveC` does not exist in 7.5.0,* the fallback is to use the `setRxCallback`/async interface; document the actual name chosen in a comment.

- [ ] **Step 5: Relax the 51-byte uplink cap**

Find `main/lorawan/lorawan_handler.cpp:355` (`if (!data || len == 0 || len > 51)`) and change to:

```cpp
if (!data || len == 0) return ESP_ERR_INVALID_ARG;
// Let RadioLib enforce the max per current DR; clamp to its declared limit
size_t max_payload = node->getMaxPayloadLen();
if (len > max_payload) {
    ESP_LOGW(TAG, "uplink %zu > max %zu at current DR — truncating to max", len, max_payload);
    len = max_payload;
}
```

- [ ] **Step 6: Build and verify**

`./build.sh build jvtech_4mb_standard` — must succeed with no warnings. Deploy to a test device: `./build.sh app` then `idf.py monitor`. Boot through OTAA join; confirm no regressions in normal uplinks.

- [ ] **Step 7: Commit**

```bash
git add main/lorawan/
git commit -m "feat(lorawan): add Class A/C switching, multicast session, run_class_c_rx"
```

## Task 1.2: TS005 package skeleton + FPort 200 handler

**Files:**
- Create: `main/fuota/ts005_multicast.h`
- Create: `main/fuota/ts005_multicast.c`

- [ ] **Step 1: Create `ts005_multicast.h`**

```c
/**
 * @file ts005_multicast.h
 * @brief LoRaWAN Remote Multicast Setup (TS005 v2.0.0), FPort 200
 *
 * The TS005 callback is driven entirely by the server. This module parses
 * McGroupSetupReq / McGroupDeleteReq / McClassCSessionReq and emits callbacks
 * to fuota_manager when a session is about to start.
 */
#ifndef TS005_MULTICAST_H
#define TS005_MULTICAST_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TS005_FPORT       200
#define TS005_PACKAGE_ID  2  // RADIOLIB_LORAWAN_PACKAGE_TS005

/**
 * @brief Per-group multicast session context (up to 4 groups per LoRa spec).
 */
typedef struct {
    bool     in_use;
    uint8_t  mc_group_id;        // 0..3
    uint32_t mc_addr;
    uint8_t  mc_app_skey[16];
    uint8_t  mc_nwk_skey[16];
    uint32_t fcnt_min;
    uint32_t fcnt_max;
} ts005_group_t;

typedef struct {
    uint8_t  mc_group_id;
    uint32_t time_to_start_s;    // seconds from now
    uint8_t  session_timeout_exp; // 2^timeout seconds
    uint32_t freq_hz;
    uint8_t  dr;
} ts005_class_c_session_t;

/**
 * @brief Called by TS005 when McClassCSessionReq arrives and must be honored.
 *        fuota_manager hooks this to schedule the actual Class-C switch at TimeToStart.
 */
typedef void (*ts005_session_start_cb_t)(const ts005_group_t *group,
                                         const ts005_class_c_session_t *session);

esp_err_t ts005_init(ts005_session_start_cb_t cb);

/**
 * @brief Register the FPort 200 handler with RadioLib.
 *        Must be called after lorawan_is_joined() == true.
 */
esp_err_t ts005_register(void);

/**
 * @brief Get group by id (read-only snapshot, thread-safe via internal mutex).
 */
bool ts005_get_group(uint8_t group_id, ts005_group_t *out);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Create `ts005_multicast.c` with CID decoders**

```c
#include "ts005_multicast.h"
#include "lorawan_handler.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "TS005";

// CIDs per TS005 v2.0.0 Table 1
enum {
    CID_PACKAGE_VERSION        = 0x00,
    CID_MC_GROUP_STATUS        = 0x01,
    CID_MC_GROUP_SETUP         = 0x02,
    CID_MC_GROUP_DELETE        = 0x03,
    CID_MC_CLASS_C_SESSION     = 0x04,
    // 0x05 = McClassBSessionReq (not supported — Class B out of scope)
};

#define TS005_MAX_GROUPS 4

static ts005_group_t s_groups[TS005_MAX_GROUPS];
static SemaphoreHandle_t s_mutex;
static ts005_session_start_cb_t s_session_cb;

static void on_downlink(uint8_t *data, size_t len)
{
    if (len < 1) return;
    uint8_t cid = data[0];
    ESP_LOGI(TAG, "TS005 downlink CID=0x%02X len=%zu", cid, len);

    switch (cid) {
        case CID_MC_GROUP_SETUP: {
            // McGroupSetupReq: CID(1) | McGroupIDHeader(1) | McAddr(4) |
            //                  McKey_encrypted(16) | MinMcFCount(4) | MaxMcFCount(4) = 30
            if (len < 30) { ESP_LOGW(TAG, "McGroupSetupReq short"); break; }
            uint8_t group_id = data[1] & 0x03;
            if (group_id >= TS005_MAX_GROUPS) break;
            if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) break;
            ts005_group_t *g = &s_groups[group_id];
            g->in_use = true;
            g->mc_group_id = group_id;
            memcpy(&g->mc_addr, &data[2], 4);
            // TS005 §2.1.2: McKey is encrypted with AppKey; derive McAppSKey/McNwkSKey
            // from McKey per §2.1.3. For the skeleton we store the raw encrypted key;
            // Task 1.3 implements derivation.
            memcpy(g->mc_app_skey, &data[6], 16);
            memset(g->mc_nwk_skey, 0, 16);   // placeholder until Task 1.3
            memcpy(&g->fcnt_min, &data[22], 4);
            memcpy(&g->fcnt_max, &data[26], 4);
            xSemaphoreGive(s_mutex);
            ESP_LOGI(TAG, "McGroupSetup id=%u addr=0x%08lX fcnt=[%lu..%lu]",
                     group_id, (unsigned long)g->mc_addr,
                     (unsigned long)g->fcnt_min, (unsigned long)g->fcnt_max);
            // TODO Task 1.3: send McGroupSetupAns on FPort 200
            break;
        }
        case CID_MC_CLASS_C_SESSION: {
            // McClassCSessionReq: CID(1)|McGroupIDHeader(1)|SessionTime(4)|
            //                     SessionTimeOut(1)|DLFrequency(3)|DR(1) = 11
            if (len < 11) { ESP_LOGW(TAG, "McClassCSessionReq short"); break; }
            uint8_t group_id = data[1] & 0x03;
            if (group_id >= TS005_MAX_GROUPS || !s_groups[group_id].in_use) break;
            ts005_class_c_session_t sess = {0};
            sess.mc_group_id = group_id;
            uint32_t session_time_gps;
            memcpy(&session_time_gps, &data[2], 4);
            // Convert GPS session time to delta seconds using clock_sync
            // (Task 1.4 wires this; for skeleton, treat as relative)
            sess.time_to_start_s = session_time_gps;
            sess.session_timeout_exp = data[6];
            // DLFrequency is 24-bit little-endian, units of 100 Hz
            uint32_t freq_raw = (uint32_t)data[7] | ((uint32_t)data[8] << 8) |
                                ((uint32_t)data[9] << 16);
            sess.freq_hz = freq_raw * 100;
            sess.dr = data[10] & 0x0F;
            ESP_LOGI(TAG, "McClassCSessionReq group=%u ToS_gps=%lu freq=%luHz DR%u",
                     group_id, (unsigned long)session_time_gps,
                     (unsigned long)sess.freq_hz, sess.dr);
            if (s_session_cb) s_session_cb(&s_groups[group_id], &sess);
            // TODO Task 1.3: send McClassCSessionAns
            break;
        }
        case CID_MC_GROUP_DELETE: {
            if (len < 2) break;
            uint8_t group_id = data[1] & 0x03;
            if (group_id < TS005_MAX_GROUPS) {
                if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                    memset(&s_groups[group_id], 0, sizeof(s_groups[group_id]));
                    xSemaphoreGive(s_mutex);
                }
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "unhandled CID 0x%02X", cid);
    }
}

esp_err_t ts005_init(ts005_session_start_cb_t cb)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) return ESP_ERR_NO_MEM;
    }
    memset(s_groups, 0, sizeof(s_groups));
    s_session_cb = cb;
    return ESP_OK;
}

esp_err_t ts005_register(void)
{
    return lorawan_add_app_package(TS005_PACKAGE_ID, on_downlink);
}

bool ts005_get_group(uint8_t group_id, ts005_group_t *out)
{
    if (group_id >= TS005_MAX_GROUPS || !out) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
    bool in_use = s_groups[group_id].in_use;
    if (in_use) memcpy(out, &s_groups[group_id], sizeof(*out));
    xSemaphoreGive(s_mutex);
    return in_use;
}
```

- [ ] **Step 3: Add to `main/CMakeLists.txt`**

Inside the `CONFIG_FUOTA_ENABLED` block, append:

```cmake
list(APPEND srcs "fuota/ts005_multicast.c")
```

- [ ] **Step 4: Wire into `fuota_task`**

In `main/fuota/fuota_manager.c`, add include and register after join:

```c
#include "ts005_multicast.h"

// near top of file
static void on_mc_session_start(const ts005_group_t *g, const ts005_class_c_session_t *s)
{
    ESP_LOGI(TAG, "Multicast session scheduled: group=%u in %lus on %luHz DR%u",
             g->mc_group_id, (unsigned long)s->time_to_start_s,
             (unsigned long)s->freq_hz, s->dr);
    // Phase 1.4 wires the actual Class-C switch here.
}

// inside fuota_task, after while(!lorawan_is_joined())
ts005_init(on_mc_session_start);
if (ts005_register() != ESP_OK) {
    ESP_LOGE(TAG, "TS005 register failed");
}
```

- [ ] **Step 5: Build + flash + commit**

```bash
./build.sh build jvtech_4mb_standard
./build.sh app
# monitor and verify TS005 logs on boot
git add main/fuota/ts005_multicast.{c,h} main/fuota/fuota_manager.c main/CMakeLists.txt
git commit -m "feat(fuota): TS005 multicast setup — FPort 200 handler skeleton"
```

## Task 1.3: TS005 McKey derivation + Ans uplinks

**Files:**
- Modify: `main/fuota/ts005_multicast.c`

The McKey in McGroupSetupReq is encrypted with the device's AppKey and must be decrypted; from the decrypted McKey, the device derives McAppSKey and McNwkSKey per TS005 §2.1.3. This task implements that derivation and the answer uplinks.

- [ ] **Step 1: Add `mbedtls/aes.h` include and a key-derivation helper**

```c
#include "mbedtls/aes.h"
#include "config_manager.h"  // for AppKey

// McKey_decrypted = aes128_decrypt(AppKey, McKey_encrypted)
// McAppSKey = aes128_encrypt(McKey, 0x01 | McAddr | 4*0x00)
// McNwkSKey = aes128_encrypt(McKey, 0x02 | McAddr | 4*0x00)
// See TS005 §2.1.3
static bool derive_mc_keys(const uint8_t mc_key_enc[16], uint32_t mc_addr,
                           uint8_t mc_app_skey[16], uint8_t mc_nwk_skey[16])
{
    const char *app_key_str = config_get_app_key();
    uint8_t app_key[16];
    // reuse hex_to_bytes from lorawan_handler.cpp — or duplicate it here as static
    extern bool hex_to_bytes(const char *hex, uint8_t *out, size_t out_len);  // declared in lorawan_handler.cpp
    if (!hex_to_bytes(app_key_str, app_key, 16)) return false;

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, app_key, 128);
    uint8_t mc_key[16];
    if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, mc_key_enc, mc_key) != 0) {
        mbedtls_aes_free(&aes);
        return false;
    }
    mbedtls_aes_free(&aes);

    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, mc_key, 128);
    uint8_t block[16] = {0};
    block[0] = 0x01;
    memcpy(&block[1], &mc_addr, 4);
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, block, mc_app_skey);
    block[0] = 0x02;
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, block, mc_nwk_skey);
    mbedtls_aes_free(&aes);
    return true;
}
```

Note: `hex_to_bytes` is currently `static` in `lorawan_handler.cpp`. Make it extern there (or duplicate it in ts005_multicast.c — the duplicate is 6 lines and arguably cleaner). Prefer making it extern via a new `lorawan_helpers.h` only if more than one TS file needs it; for now, duplicate.

- [ ] **Step 2: Call derivation in `CID_MC_GROUP_SETUP` branch**

Replace the placeholder:

```c
// replace: memcpy(g->mc_app_skey, &data[6], 16); memset(g->mc_nwk_skey, 0, 16);
uint8_t mc_key_enc[16];
memcpy(mc_key_enc, &data[6], 16);
if (!derive_mc_keys(mc_key_enc, g->mc_addr, g->mc_app_skey, g->mc_nwk_skey)) {
    ESP_LOGE(TAG, "McKey derivation failed");
    g->in_use = false;
    xSemaphoreGive(s_mutex);
    break;
}
```

- [ ] **Step 3: Send McGroupSetupAns and McClassCSessionAns uplinks**

Per TS005 §2.2 and §2.4, answer format:

```c
// McGroupSetupAns: CID(1) | StatusID(1)
// StatusID bit0 = IDError (1 if McGroupID invalid), bit1 = McGroupUndefined
static void send_group_setup_ans(uint8_t group_id, bool id_error)
{
    uint8_t ans[2];
    ans[0] = CID_MC_GROUP_SETUP;
    ans[1] = (id_error ? 0x01 : 0x00) | ((group_id & 0x03) << 0);
    lorawan_send(ans, sizeof(ans), TS005_FPORT, false);
}

// McClassCSessionAns: CID(1) | StatusAndMcGroupID(1) | TimeToStart(3)
static void send_class_c_session_ans(uint8_t group_id, uint32_t time_to_start_s, bool err)
{
    uint8_t ans[5];
    ans[0] = CID_MC_CLASS_C_SESSION;
    ans[1] = (err ? 0x04 : 0x00) | (group_id & 0x03);
    ans[2] = (uint8_t)(time_to_start_s & 0xFF);
    ans[3] = (uint8_t)((time_to_start_s >> 8) & 0xFF);
    ans[4] = (uint8_t)((time_to_start_s >> 16) & 0xFF);
    lorawan_send(ans, sizeof(ans), TS005_FPORT, false);
}
```

Call each from the respective CID branch after processing. **Do not call from within the downlink callback directly** — lorawan_send holds the same mutex and will deadlock. Defer via a small FreeRTOS queue:

```c
typedef struct {
    uint8_t type;  // 0=group_setup_ans, 1=class_c_session_ans
    uint8_t group_id;
    uint32_t aux;   // time_to_start_s for type=1
    bool error;
} ts005_ans_msg_t;

static QueueHandle_t s_ans_queue;

static void ts005_ans_task(void *param) {
    ts005_ans_msg_t msg;
    while (1) {
        if (xQueueReceive(s_ans_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.type == 0) send_group_setup_ans(msg.group_id, msg.error);
            else if (msg.type == 1) send_class_c_session_ans(msg.group_id, msg.aux, msg.error);
        }
    }
}
```

Create queue and task inside `ts005_init`:

```c
s_ans_queue = xQueueCreate(4, sizeof(ts005_ans_msg_t));
xTaskCreatePinnedToCore(ts005_ans_task, "ts005_ans", 4096, NULL, 3, NULL, 0);
```

Replace `lorawan_send(...)` direct calls in the downlink handler with `xQueueSend(s_ans_queue, ...)`.

- [ ] **Step 4: Build and commit**

```bash
./build.sh build jvtech_4mb_standard
git add main/fuota/ts005_multicast.c
git commit -m "feat(fuota): TS005 McKey derivation and answer uplinks via queue"
```

## Task 1.4: Wire Class-C switch at `TimeToStart`

**Files:**
- Modify: `main/fuota/fuota_manager.c`

- [ ] **Step 1: Add GPS-time → delta conversion**

The McClassCSessionReq field is a 32-bit GPS epoch seconds value (time the session starts). Use `clock_sync_get_time()` + GPS offset to compute delta:

```c
#include "clock_sync.h"
#define GPS_TO_UNIX_OFFSET 315964800UL

static uint32_t gps_seconds_until(uint32_t gps_target)
{
    time_t now = clock_sync_get_time();
    if (now == 0) return UINT32_MAX;  // not synced — cannot schedule
    uint32_t gps_now = (uint32_t)(now - GPS_TO_UNIX_OFFSET);
    return (gps_target > gps_now) ? (gps_target - gps_now) : 0;
}
```

**Important:** TS005 requires the device clock be synced for multicast sessions to work reliably. The plan assumes TS003 clock sync (already implemented) has run successfully within the last 24h. If not synced, `on_mc_session_start` must refuse the session with StatusAndMcGroupID's DR-error bit (see TS005 §2.4.2).

- [ ] **Step 2: Implement scheduled switch**

Replace the `on_mc_session_start` stub from Task 1.2:

```c
#include "ts005_multicast.h"

typedef struct {
    ts005_group_t group;
    ts005_class_c_session_t session;
} mc_session_ctx_t;

static void mc_session_task(void *arg)
{
    mc_session_ctx_t *ctx = (mc_session_ctx_t *)arg;
    uint32_t wait_s = gps_seconds_until(ctx->session.time_to_start_s);
    if (wait_s == UINT32_MAX) {
        ESP_LOGE(TAG, "Cannot schedule multicast — clock not synced");
        free(ctx);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Sleeping %lus until multicast session", (unsigned long)wait_s);
    vTaskDelay(pdMS_TO_TICKS(wait_s * 1000));

    // Start session in the lorawan stack
    if (lorawan_start_multicast_session(ctx->group.mc_addr,
                                        ctx->group.mc_app_skey,
                                        ctx->group.mc_nwk_skey,
                                        ctx->group.fcnt_min,
                                        ctx->group.fcnt_max,
                                        ctx->session.freq_hz,
                                        ctx->session.dr) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start multicast session");
        free(ctx);
        vTaskDelete(NULL);
        return;
    }
    lorawan_switch_class(LORAWAN_CLASS_C);

    // Receive until session timeout
    uint32_t timeout_s = 1UL << ctx->session.session_timeout_exp;
    uint32_t end_tick = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_s * 1000);
    ESP_LOGI(TAG, "Class C RX for %lus", (unsigned long)timeout_s);
    while (xTaskGetTickCount() < end_tick) {
        lorawan_run_class_c_rx(1000);
    }

    // Tear down
    lorawan_switch_class(LORAWAN_CLASS_A);
    lorawan_stop_multicast_session();
    ESP_LOGI(TAG, "Multicast session ended");
    free(ctx);
    vTaskDelete(NULL);
}

static void on_mc_session_start(const ts005_group_t *g, const ts005_class_c_session_t *s)
{
    mc_session_ctx_t *ctx = (mc_session_ctx_t *)malloc(sizeof(*ctx));
    if (!ctx) return;
    ctx->group = *g;
    ctx->session = *s;
    xTaskCreatePinnedToCore(mc_session_task, "mc_sess", 6144, ctx, 4, NULL, 0);
}
```

- [ ] **Step 3: Build, flash, and run HIL smoke test**

Deploy to a test device. In ChirpStack's fuota-server, trigger a multicast session with a tiny payload (e.g. 10 bytes on a dummy FPort). The UART should log:

```
TS005 downlink CID=0x02 ...
McGroupSetup id=0 addr=...
TS005 downlink CID=0x04 ...
McClassCSessionReq group=0 ...
Multicast session scheduled: group=0 in <N>s ...
Sleeping <N>s until multicast session
Multicast session started (addr=... freq=... DR10)
Switched to Class C
[receives test payload]
Switched to Class A
Multicast session ended
```

Verification criteria: the test payload is received during the Class C window (logged via whatever FPort it came in on — for smoke test, use a generic handler that logs any unhandled downlink).

- [ ] **Step 4: Commit**

```bash
git add main/fuota/fuota_manager.c
git commit -m "feat(fuota): schedule Class-C multicast session at TimeToStart"
```

### Phase 1 Acceptance

- [ ] ChirpStack deploys an empty multicast session; device receives the test payload in Class C.
- [ ] After the session's SessionTimeOut, device returns to Class A and a normal uplink succeeds.
- [ ] No regressions: all 4 SKUs build; 2MB devices are unaffected (TS005 code is excluded).

---

# Phase 2 — TS004 Fragmented Data Block Transport (≈ 1.5 weeks)

TS004 is the most algorithmically complex package: it receives N data fragments plus K redundancy (parity) fragments and reconstructs the payload even with packet loss up to K fragments. The forward-error-correction scheme is a Gaussian-elimination matrix decoder (TS004 §2.5).

## Task 2.1: Fragment writer to inactive OTA partition

**Files:**
- Create: `main/fuota/fuota_flash.h`
- Create: `main/fuota/fuota_flash.c`

The writer stores fragments in the inactive OTA partition (ota_1 if running from ota_0, and vice versa) and maintains a received-fragment bitmap in NVS so a power loss during download can resume.

- [ ] **Step 1: Create `fuota_flash.h`**

```c
/**
 * @file fuota_flash.h
 * @brief Incremental fragment writer to inactive OTA partition.
 *
 * Tracks received fragments via a bitmap persisted to NVS so a power loss
 * mid-download can resume (see fuota_flash_resume_possible).
 *
 * Fragment layout on flash:
 *   [fragment 0][fragment 1]...[fragment N-1]
 * at offsets frag_index * frag_size. Padding is only applied once all
 * fragments are received (TS004 §2.5: last fragment may be shorter).
 */
#ifndef FUOTA_FLASH_H
#define FUOTA_FLASH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t fuota_flash_begin(uint16_t nb_frag, uint8_t frag_size, uint32_t session_id);
esp_err_t fuota_flash_write_fragment(uint16_t frag_index, const uint8_t *data, size_t len);
bool      fuota_flash_has_fragment(uint16_t frag_index);
uint16_t  fuota_flash_fragments_received(void);
esp_err_t fuota_flash_finalize(const uint8_t *sha256_expected);
esp_err_t fuota_flash_abort(void);
bool      fuota_flash_resume_possible(uint32_t session_id);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Create `fuota_flash.c`**

```c
#include "fuota_flash.h"
#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/sha256.h"

static const char *TAG = "FUOTA_FLASH";
#define NVS_NS   "fuota_flash"
#define NVS_KEY_SESSION "session"
#define NVS_KEY_NB      "nb_frag"
#define NVS_KEY_SIZE    "frag_size"
#define NVS_KEY_BITMAP  "bitmap"

static const esp_partition_t *s_target;
static uint16_t s_nb_frag;
static uint8_t  s_frag_size;
static uint32_t s_session_id;
static uint8_t *s_bitmap;      // ceil(nb_frag/8) bytes
static size_t   s_bitmap_len;
static bool     s_erased;

static bool bitmap_get(uint16_t i) {
    return s_bitmap && (s_bitmap[i >> 3] & (1u << (i & 7)));
}
static void bitmap_set(uint16_t i) {
    if (s_bitmap) s_bitmap[i >> 3] |= (uint8_t)(1u << (i & 7));
}

static esp_err_t persist_bitmap(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_blob(h, NVS_KEY_BITMAP, s_bitmap, s_bitmap_len);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t fuota_flash_begin(uint16_t nb_frag, uint8_t frag_size, uint32_t session_id)
{
    if (nb_frag == 0 || frag_size == 0) return ESP_ERR_INVALID_ARG;

    s_target = esp_ota_get_next_update_partition(NULL);
    if (!s_target) {
        ESP_LOGE(TAG, "no next_update_partition available");
        return ESP_ERR_NOT_FOUND;
    }
    size_t total = (size_t)nb_frag * frag_size;
    if (total > s_target->size) {
        ESP_LOGE(TAG, "image (%zu) > partition (%u)", total, (unsigned)s_target->size);
        return ESP_ERR_INVALID_SIZE;
    }

    // Resume?
    bool resuming = fuota_flash_resume_possible(session_id);
    s_nb_frag = nb_frag;
    s_frag_size = frag_size;
    s_session_id = session_id;
    s_bitmap_len = (nb_frag + 7) / 8;
    free(s_bitmap);
    s_bitmap = calloc(1, s_bitmap_len);
    if (!s_bitmap) return ESP_ERR_NO_MEM;

    if (resuming) {
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            size_t len = s_bitmap_len;
            nvs_get_blob(h, NVS_KEY_BITMAP, s_bitmap, &len);
            nvs_close(h);
        }
        s_erased = true;  // assume previously erased
        ESP_LOGI(TAG, "Resuming session 0x%08lX (%u fragments known received)",
                 (unsigned long)session_id, fuota_flash_fragments_received());
        return ESP_OK;
    }

    // Fresh session: erase + record session-id
    ESP_LOGI(TAG, "Erasing %s (%u bytes)...", s_target->label, (unsigned)s_target->size);
    esp_err_t err = esp_partition_erase_range(s_target, 0, s_target->size);
    if (err != ESP_OK) return err;
    s_erased = true;

    nvs_handle_t h;
    nvs_open(NVS_NS, NVS_READWRITE, &h);
    nvs_set_u32(h, NVS_KEY_SESSION, session_id);
    nvs_set_u16(h, NVS_KEY_NB, nb_frag);
    nvs_set_u8(h, NVS_KEY_SIZE, frag_size);
    nvs_set_blob(h, NVS_KEY_BITMAP, s_bitmap, s_bitmap_len);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t fuota_flash_write_fragment(uint16_t frag_index, const uint8_t *data, size_t len)
{
    if (!s_target || !s_bitmap) return ESP_ERR_INVALID_STATE;
    if (frag_index >= s_nb_frag) return ESP_ERR_INVALID_ARG;
    if (len > s_frag_size) return ESP_ERR_INVALID_SIZE;
    if (bitmap_get(frag_index)) return ESP_OK;  // dup

    size_t offset = (size_t)frag_index * s_frag_size;
    esp_err_t err = esp_partition_write(s_target, offset, data, len);
    if (err != ESP_OK) { ESP_LOGE(TAG, "write fail: %s", esp_err_to_name(err)); return err; }
    bitmap_set(frag_index);

    // Persist bitmap every 16 fragments (reduces flash wear while still
    // providing resume granularity after power loss)
    if ((frag_index & 0x0F) == 0) persist_bitmap();
    return ESP_OK;
}

bool fuota_flash_has_fragment(uint16_t frag_index)
{
    return bitmap_get(frag_index);
}

uint16_t fuota_flash_fragments_received(void)
{
    if (!s_bitmap) return 0;
    uint16_t n = 0;
    for (uint16_t i = 0; i < s_nb_frag; i++) if (bitmap_get(i)) n++;
    return n;
}

esp_err_t fuota_flash_finalize(const uint8_t *sha256_expected)
{
    if (!s_target) return ESP_ERR_INVALID_STATE;
    persist_bitmap();

    // Verify hash over the full image area
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    uint8_t buf[512];
    size_t total = (size_t)s_nb_frag * s_frag_size;
    for (size_t off = 0; off < total; off += sizeof(buf)) {
        size_t chunk = (total - off > sizeof(buf)) ? sizeof(buf) : (total - off);
        esp_partition_read(s_target, off, buf, chunk);
        mbedtls_sha256_update(&ctx, buf, chunk);
    }
    uint8_t sha[32];
    mbedtls_sha256_finish(&ctx, sha);
    mbedtls_sha256_free(&ctx);

    if (sha256_expected && memcmp(sha, sha256_expected, 32) != 0) {
        ESP_LOGE(TAG, "SHA-256 mismatch");
        return ESP_ERR_INVALID_CRC;
    }
    ESP_LOGI(TAG, "SHA-256 OK");
    return ESP_OK;
}

esp_err_t fuota_flash_abort(void)
{
    free(s_bitmap); s_bitmap = NULL;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    s_target = NULL;
    s_erased = false;
    return ESP_OK;
}

bool fuota_flash_resume_possible(uint32_t session_id)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    uint32_t saved = 0;
    bool ok = (nvs_get_u32(h, NVS_KEY_SESSION, &saved) == ESP_OK) && (saved == session_id);
    nvs_close(h);
    return ok;
}
```

- [ ] **Step 3: Add to CMake and build**

```cmake
list(APPEND srcs "fuota/fuota_flash.c")
```

Add `"app_update"` to the `requires` list at the top of `main/CMakeLists.txt` (already present — confirm).

Build: `./build.sh build jvtech_4mb_standard` — must succeed.

- [ ] **Step 4: Commit**

```bash
git add main/fuota/fuota_flash.{c,h} main/CMakeLists.txt
git commit -m "feat(fuota): fragment writer to inactive OTA partition with NVS bitmap"
```

## Task 2.2: TS004 CID decoders + fragment ingest

**Files:**
- Create: `main/fuota/ts004_fragmentation.h`
- Create: `main/fuota/ts004_fragmentation.c`

- [ ] **Step 1: Create `ts004_fragmentation.h`**

```c
/**
 * @file ts004_fragmentation.h
 * @brief LoRaWAN Fragmented Data Block Transport (TS004 v2.0.0), FPort 201
 */
#ifndef TS004_FRAGMENTATION_H
#define TS004_FRAGMENTATION_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TS004_FPORT       201
#define TS004_PACKAGE_ID  3  // RADIOLIB_LORAWAN_PACKAGE_TS004

typedef struct {
    uint32_t session_id;         // low 16 bits is SessionCount from spec
    uint16_t nb_frag;
    uint8_t  frag_size;
    uint16_t padding;            // trailing pad bytes in last fragment
    uint32_t descriptor;         // opaque to TS004; passed to fuota_manager
} ts004_session_t;

typedef void (*ts004_session_ready_cb_t)(const ts004_session_t *sess);
typedef void (*ts004_complete_cb_t)(const ts004_session_t *sess);

esp_err_t ts004_init(ts004_session_ready_cb_t on_ready, ts004_complete_cb_t on_complete);
esp_err_t ts004_register(void);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Create `ts004_fragmentation.c` — CID decoders**

Start with setup/delete/status/data handlers (no FEC yet — that's Task 2.3). CID constants per TS004 v2.0.0 Table 2:

```c
#include "ts004_fragmentation.h"
#include "fuota_flash.h"
#include "lorawan_handler.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "TS004";

enum {
    CID_PACKAGE_VERSION       = 0x00,
    CID_FRAG_SESSION_STATUS   = 0x01,
    CID_FRAG_SESSION_SETUP    = 0x02,
    CID_FRAG_SESSION_DELETE   = 0x03,
    CID_DATA_FRAGMENT         = 0x08,
};

static ts004_session_t s_sess;
static bool s_sess_active;
static ts004_session_ready_cb_t s_on_ready;
static ts004_complete_cb_t      s_on_complete;

// Deferred-uplink queue (same rationale as TS005 Task 1.3)
typedef struct {
    uint8_t  cid;
    uint8_t  data[8];
    uint8_t  len;
} ts004_ans_msg_t;
static QueueHandle_t s_ans_queue;

static void ts004_ans_task(void *_) {
    ts004_ans_msg_t m;
    while (xQueueReceive(s_ans_queue, &m, portMAX_DELAY) == pdTRUE) {
        lorawan_send(m.data, m.len, TS004_FPORT, false);
    }
}

static void on_setup(uint8_t *data, size_t len)
{
    // FragSessionSetupReq:
    //   CID(1) | FragSession(1) | NbFrag(2) | FragSize(1) | ControlAndPadding(1)
    //   | Descriptor(4) | MIC(4) = 14 bytes (v2.0.0)
    if (len < 14) return;
    uint8_t frag_session = data[1];  // bits[5:4]=FragSessionIndex, bits[3:0]=McGroupBitMask
    uint16_t nb_frag = (uint16_t)data[2] | ((uint16_t)(data[3] & 0x3F) << 8);
    uint8_t frag_size = data[4];
    uint8_t control = data[5];
    uint16_t padding = 0;  // spec: padding is derived, not sent directly in v2.0.0
    uint32_t descriptor;
    memcpy(&descriptor, &data[6], 4);
    // MIC at data[10..13] not validated here (server-side FUOTA proves it)

    s_sess.session_id = ((uint32_t)frag_session << 16) | (descriptor & 0xFFFF);
    s_sess.nb_frag = nb_frag;
    s_sess.frag_size = frag_size;
    s_sess.padding = padding;
    s_sess.descriptor = descriptor;
    s_sess_active = true;

    esp_err_t err = fuota_flash_begin(nb_frag, frag_size, s_sess.session_id);
    uint8_t ans[2] = { CID_FRAG_SESSION_SETUP, err == ESP_OK ? 0x00 : 0x01 };
    ts004_ans_msg_t m = { .cid = CID_FRAG_SESSION_SETUP, .len = 2 };
    memcpy(m.data, ans, 2);
    xQueueSend(s_ans_queue, &m, 0);

    if (err == ESP_OK && s_on_ready) s_on_ready(&s_sess);
}

static void on_fragment(uint8_t *data, size_t len)
{
    // DataFragment: CID(1) | Index&N(2, little-endian) | Payload(N bytes)
    if (len < 3 || !s_sess_active) return;
    uint16_t indexN;
    memcpy(&indexN, &data[1], 2);
    uint16_t n = indexN & 0x3FFF;   // low 14 bits = fragment index (1-based per spec)
    // FragSessionIndex in top 2 bits — ignore (we only support one session)
    size_t payload_len = len - 3;
    if (n == 0 || n > s_sess.nb_frag) return;

    uint16_t idx0 = n - 1;  // convert to 0-based
    if (idx0 < s_sess.nb_frag) {
        // Regular fragment (not parity) — store directly
        fuota_flash_write_fragment(idx0, &data[3], payload_len);
    } else {
        // Parity fragment — handled in Task 2.3 (FEC decoder)
    }

    uint16_t got = fuota_flash_fragments_received();
    if ((got & 0x3F) == 0) ESP_LOGI(TAG, "fragments: %u/%u", got, s_sess.nb_frag);

    if (got >= s_sess.nb_frag) {
        ESP_LOGI(TAG, "all fragments received");
        s_sess_active = false;
        if (s_on_complete) s_on_complete(&s_sess);
    }
}

static void on_status(uint8_t *data, size_t len)
{
    // FragSessionStatusReq: CID(1) | StatusParams(1) — bit7 = participants
    if (len < 2 || !s_sess_active) return;
    uint16_t missing = 0;
    uint16_t first_missing = 0;
    bool found_first = false;
    for (uint16_t i = 0; i < s_sess.nb_frag; i++) {
        if (!fuota_flash_has_fragment(i)) {
            missing++;
            if (!found_first) { first_missing = i + 1; found_first = true; }
        }
    }
    // FragSessionStatusAns: CID(1) | ReceivedAndIndex(2) | MissingFrag(1) | Status(1) = 5
    uint8_t ans[5];
    ans[0] = CID_FRAG_SESSION_STATUS;
    uint16_t recv_idx = (uint16_t)((found_first ? first_missing : 0) & 0x3FFF);
    ans[1] = (uint8_t)(recv_idx & 0xFF);
    ans[2] = (uint8_t)((recv_idx >> 8) & 0x3F);
    ans[3] = (uint8_t)(missing > 0xFF ? 0xFF : missing);
    ans[4] = 0x00;
    ts004_ans_msg_t m = { .cid = CID_FRAG_SESSION_STATUS, .len = 5 };
    memcpy(m.data, ans, 5);
    xQueueSend(s_ans_queue, &m, 0);
}

static void on_delete(uint8_t *data, size_t len)
{
    if (len < 2) return;
    fuota_flash_abort();
    s_sess_active = false;
    uint8_t ans[2] = { CID_FRAG_SESSION_DELETE, 0x00 };
    ts004_ans_msg_t m = { .cid = CID_FRAG_SESSION_DELETE, .len = 2 };
    memcpy(m.data, ans, 2);
    xQueueSend(s_ans_queue, &m, 0);
}

static void on_downlink(uint8_t *data, size_t len)
{
    if (len < 1) return;
    switch (data[0]) {
        case CID_FRAG_SESSION_SETUP:  on_setup(data, len);    break;
        case CID_FRAG_SESSION_DELETE: on_delete(data, len);   break;
        case CID_FRAG_SESSION_STATUS: on_status(data, len);   break;
        case CID_DATA_FRAGMENT:       on_fragment(data, len); break;
        default: ESP_LOGW(TAG, "unhandled CID 0x%02X", data[0]);
    }
}

esp_err_t ts004_init(ts004_session_ready_cb_t on_ready, ts004_complete_cb_t on_complete)
{
    s_on_ready = on_ready;
    s_on_complete = on_complete;
    s_sess_active = false;
    if (!s_ans_queue) s_ans_queue = xQueueCreate(8, sizeof(ts004_ans_msg_t));
    if (!s_ans_queue) return ESP_ERR_NO_MEM;
    xTaskCreatePinnedToCore(ts004_ans_task, "ts004_ans", 4096, NULL, 3, NULL, 0);
    return ESP_OK;
}

esp_err_t ts004_register(void)
{
    return lorawan_add_app_package(TS004_PACKAGE_ID, on_downlink);
}
```

- [ ] **Step 3: Wire into `fuota_manager.c`**

```c
#include "ts004_fragmentation.h"

static void on_ts004_ready(const ts004_session_t *s) {
    ESP_LOGI(TAG, "TS004 session ready: %u frags x %u B, desc=0x%08lX",
             s->nb_frag, s->frag_size, (unsigned long)s->descriptor);
}
static void on_ts004_complete(const ts004_session_t *s) {
    ESP_LOGI(TAG, "TS004 session complete — passing to TS006 install (Phase 3)");
}

// in fuota_task after ts005_register():
ts004_init(on_ts004_ready, on_ts004_complete);
ts004_register();
```

- [ ] **Step 4: Add to CMake, build, commit**

```cmake
list(APPEND srcs "fuota/ts004_fragmentation.c")
```

```bash
./build.sh build jvtech_4mb_standard
git add main/fuota/ts004_fragmentation.{c,h} main/fuota/fuota_manager.c main/CMakeLists.txt
git commit -m "feat(fuota): TS004 fragmentation — setup/delete/status/data handlers"
```

## Task 2.3: Forward error correction (parity fragment decoding)

**Files:**
- Modify: `main/fuota/ts004_fragmentation.c`
- Create: `main/fuota/ts004_fec.c`
- Create: `main/fuota/ts004_fec.h`

TS004 §2.5 specifies a matrix-based FEC: the server sends up to K redundancy fragments, each a linear combination (XOR) of a subset of data fragments determined by a pseudorandom generator. The device solves a sparse system over GF(2) to recover missing data fragments.

This is the single densest algorithmic task in the plan. Reference: TS004 v2.0.0 §2.5.3 "Parity fragments computation" — the generator is fully specified.

- [ ] **Step 1: Create `ts004_fec.h`**

```c
/**
 * @file ts004_fec.h
 * @brief TS004 §2.5 forward error correction (Gaussian elimination over GF(2)).
 *
 * Usage:
 *   ts004_fec_ctx_t *c = ts004_fec_create(nb_frag, frag_size);
 *   // On each parity fragment received (index > nb_frag):
 *   ts004_fec_feed_parity(c, parity_index, parity_payload);
 *   // Call after each fragment (data or parity) to attempt recovery:
 *   ts004_fec_try_recover(c);  // fills missing data fragments via fuota_flash_write_fragment
 *   ts004_fec_destroy(c);
 */
#ifndef TS004_FEC_H
#define TS004_FEC_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ts004_fec_ctx ts004_fec_ctx_t;

ts004_fec_ctx_t *ts004_fec_create(uint16_t nb_frag, uint8_t frag_size);
esp_err_t        ts004_fec_feed_parity(ts004_fec_ctx_t *c, uint16_t parity_index, const uint8_t *payload);
bool             ts004_fec_try_recover(ts004_fec_ctx_t *c);
void             ts004_fec_destroy(ts004_fec_ctx_t *c);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Implement `ts004_fec.c`**

The pseudo-random row generator per TS004 §2.5.3 — the Matrix Line Rand (MLR) is a specific LCG:

```c
#include "ts004_fec.h"
#include "fuota_flash.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "TS004_FEC";

struct ts004_fec_ctx {
    uint16_t nb_frag;
    uint8_t  frag_size;
    // For each parity fragment, store:
    //   - the coefficient vector (which data fragments it XORs) as a bitmap
    //   - the parity payload XORed with all the already-known data fragments
    // Then Gaussian-eliminate across parity rows once we have enough
    uint16_t max_parity;
    uint16_t parity_count;
    uint8_t  **rows;             // parity_count rows, each (nb_frag/8 + frag_size) bytes: [coef bitmap][parity payload]
};

// TS004 v2.0.0 §2.5.3 "Matrix Line Generator"
// M = parity_index + 1 (spec uses 1-based parity numbering)
// Pseudo-random sequence generated via LFSR
static void fec_matrix_line(uint16_t nb_frag, uint16_t M, uint8_t *coef_bitmap)
{
    uint16_t bytes = (nb_frag + 7) / 8;
    memset(coef_bitmap, 0, bytes);

    uint16_t m = 1;
    uint32_t x = 1 + 1001 * M;
    // number of "1"s in the generated row = nb_frag/2 (per spec)
    uint16_t nbCoeff = nb_frag / 2;

    uint16_t selected = 0;
    while (selected < nbCoeff) {
        // LFSR step
        uint16_t r = 1 << 16;
        while (r >= nb_frag) {
            x = (x * 1103515245 + 12345) & 0x7FFFFFFF;
            r = (x >> 16) & 0xFFFF;
            r &= (nb_frag - 1);   // only works if nb_frag is power of 2 — spec uses modulo
            r = r % nb_frag;
        }
        uint8_t mask = 1u << (r & 7);
        if (!(coef_bitmap[r >> 3] & mask)) {
            coef_bitmap[r >> 3] |= mask;
            selected++;
        }
    }
}

ts004_fec_ctx_t *ts004_fec_create(uint16_t nb_frag, uint8_t frag_size)
{
    ts004_fec_ctx_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->nb_frag = nb_frag;
    c->frag_size = frag_size;
    c->max_parity = nb_frag;  // practical upper bound
    c->rows = calloc(c->max_parity, sizeof(uint8_t *));
    return c;
}

esp_err_t ts004_fec_feed_parity(ts004_fec_ctx_t *c, uint16_t parity_index, const uint8_t *payload)
{
    if (!c || parity_index <= c->nb_frag) return ESP_ERR_INVALID_ARG;
    uint16_t M = parity_index - c->nb_frag;  // 1-based parity number
    if (c->parity_count >= c->max_parity) return ESP_ERR_NO_MEM;

    uint16_t coef_bytes = (c->nb_frag + 7) / 8;
    size_t row_size = coef_bytes + c->frag_size;
    uint8_t *row = calloc(1, row_size);
    if (!row) return ESP_ERR_NO_MEM;

    fec_matrix_line(c->nb_frag, M, row);
    memcpy(row + coef_bytes, payload, c->frag_size);

    // XOR known data fragments out of the parity (reduces row to unknowns only)
    for (uint16_t i = 0; i < c->nb_frag; i++) {
        if (fuota_flash_has_fragment(i) && (row[i >> 3] & (1u << (i & 7)))) {
            uint8_t frag[256];  // max frag_size is 255
            // Re-read fragment from flash — slow but memory-cheap
            extern esp_err_t fuota_flash_read_fragment(uint16_t, uint8_t *, size_t);
            fuota_flash_read_fragment(i, frag, c->frag_size);
            for (size_t k = 0; k < c->frag_size; k++) {
                row[coef_bytes + k] ^= frag[k];
            }
            row[i >> 3] &= ~(1u << (i & 7));  // clear coefficient
        }
    }
    c->rows[c->parity_count++] = row;
    return ESP_OK;
}

bool ts004_fec_try_recover(ts004_fec_ctx_t *c)
{
    if (!c || c->parity_count == 0) return false;
    uint16_t coef_bytes = (c->nb_frag + 7) / 8;
    size_t row_size = coef_bytes + c->frag_size;

    // Gaussian elimination: for each row, find its first non-zero coefficient,
    // and eliminate that column from all other rows.
    bool progress = true;
    while (progress) {
        progress = false;
        for (uint16_t r = 0; r < c->parity_count; r++) {
            uint8_t *row = c->rows[r];
            if (!row) continue;
            // Find first set bit in coefficients
            int pivot = -1;
            for (uint16_t i = 0; i < c->nb_frag; i++) {
                if (row[i >> 3] & (1u << (i & 7))) { pivot = i; break; }
            }
            if (pivot < 0) continue;  // row is zero — discard or noop
            // Is this the only bit? Then we can recover fragment `pivot`.
            bool singleton = true;
            for (uint16_t i = pivot + 1; i < c->nb_frag; i++) {
                if (row[i >> 3] & (1u << (i & 7))) { singleton = false; break; }
            }
            if (singleton) {
                // row[coef_bytes..] is the recovered fragment content
                if (!fuota_flash_has_fragment(pivot)) {
                    fuota_flash_write_fragment(pivot, row + coef_bytes, c->frag_size);
                    ESP_LOGI(TAG, "FEC recovered fragment %u", pivot);
                    progress = true;
                }
                // Eliminate this pivot from every other row
                for (uint16_t r2 = 0; r2 < c->parity_count; r2++) {
                    if (r2 == r) continue;
                    uint8_t *row2 = c->rows[r2];
                    if (!row2) continue;
                    if (row2[pivot >> 3] & (1u << (pivot & 7))) {
                        for (size_t k = 0; k < c->frag_size; k++) {
                            row2[coef_bytes + k] ^= row[coef_bytes + k];
                        }
                        row2[pivot >> 3] &= ~(1u << (pivot & 7));
                    }
                }
                free(c->rows[r]); c->rows[r] = NULL;
            }
        }
    }
    return true;
}

void ts004_fec_destroy(ts004_fec_ctx_t *c)
{
    if (!c) return;
    for (uint16_t i = 0; i < c->parity_count; i++) free(c->rows[i]);
    free(c->rows);
    free(c);
}
```

**Caveat for the executor:** the `fec_matrix_line` implementation above follows the *shape* of TS004 §2.5.3 but uses a simplified LFSR. Before committing, cross-check against either (a) the LoRa Alliance reference C code distributed with TS004, or (b) the implementation in chirpstack-fuota-server source (<https://github.com/chirpstack/chirpstack-fuota-server>). The generator must match the server byte-for-byte or recovery will fail silently.

- [ ] **Step 3: Add `fuota_flash_read_fragment`**

In `main/fuota/fuota_flash.c`, add:

```c
esp_err_t fuota_flash_read_fragment(uint16_t frag_index, uint8_t *out, size_t len)
{
    if (!s_target || frag_index >= s_nb_frag || len > s_frag_size) return ESP_ERR_INVALID_ARG;
    return esp_partition_read(s_target, (size_t)frag_index * s_frag_size, out, len);
}
```

Declare in `fuota_flash.h`.

- [ ] **Step 4: Hook parity into `on_fragment` in `ts004_fragmentation.c`**

Replace the TODO comment in `on_fragment` (from Task 2.2):

```c
    if (idx0 < s_sess.nb_frag) {
        fuota_flash_write_fragment(idx0, &data[3], payload_len);
    } else {
        // Parity fragment — feed to FEC
        static ts004_fec_ctx_t *fec_ctx;
        if (!fec_ctx) fec_ctx = ts004_fec_create(s_sess.nb_frag, s_sess.frag_size);
        if (fec_ctx) {
            ts004_fec_feed_parity(fec_ctx, n, &data[3]);
            ts004_fec_try_recover(fec_ctx);
        }
    }
```

On session end (complete or delete), destroy `fec_ctx` and null it.

- [ ] **Step 5: Build, commit**

```bash
./build.sh build jvtech_4mb_standard
git add main/fuota/ts004_fec.{c,h} main/fuota/fuota_flash.{c,h} main/fuota/ts004_fragmentation.c
git commit -m "feat(fuota): TS004 FEC decoder (Gaussian elimination over GF(2))"
```

## Task 2.4: Unity host test for FEC reassembly

**Files:**
- Create: `test/CMakeLists.txt`
- Create: `test/test_ts004_reassembly.c`

- [ ] **Step 1: Scaffold the test component**

Follow the ESP-IDF native test app pattern: <https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32/api-guides/unit-tests.html>

Create `test/CMakeLists.txt`:

```cmake
set(srcs "test_ts004_reassembly.c")
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES unity main esp_partition
)
```

Root-level `test_app` project layout follows the stock IDF template.

- [ ] **Step 2: Write the test**

`test/test_ts004_reassembly.c` — creates a synthetic 4KB image, splits into 32 fragments of 128 B, drops 4 random ones, feeds parity fragments until FEC recovers them, asserts reconstructed == original.

```c
#include "unity.h"
#include "ts004_fec.h"
#include "fuota_flash.h"
#include <string.h>
#include <stdlib.h>

// Stub: fuota_flash replaced by an in-memory array for host tests
static uint8_t  g_mem[4096];
static uint8_t  g_bitmap[4];  // 32 bits

TEST_CASE("TS004 FEC recovers 4 dropped fragments with 8 parity", "[ts004]")
{
    const uint16_t NB = 32;
    const uint8_t  SZ = 128;
    uint8_t original[NB * SZ];
    for (size_t i = 0; i < sizeof(original); i++) original[i] = (uint8_t)(i * 7 + 3);

    memcpy(g_mem, original, sizeof(original));
    // Drop fragments 5, 10, 17, 28
    const int drop[] = {5, 10, 17, 28};
    for (int d = 0; d < 4; d++) memset(g_mem + drop[d]*SZ, 0, SZ);
    // Set bitmap for present fragments
    for (int i = 0; i < NB; i++) {
        bool dropped = false;
        for (int d = 0; d < 4; d++) if (drop[d] == i) dropped = true;
        if (!dropped) g_bitmap[i >> 3] |= (1 << (i & 7));
    }

    // Compute parity fragments per TS004 §2.5.3 (see chirpstack-fuota-server)
    // ... (test vector either generated or hard-coded from server output) ...

    // Feed parity to FEC, assert recovery
    ts004_fec_ctx_t *c = ts004_fec_create(NB, SZ);
    TEST_ASSERT_NOT_NULL(c);
    // for each parity P[m]:
    //   ts004_fec_feed_parity(c, NB + m, parity[m]);
    //   ts004_fec_try_recover(c);
    // Assert g_mem == original after recovery
    TEST_ASSERT_EQUAL_MEMORY(original, g_mem, sizeof(original));
    ts004_fec_destroy(c);
}
```

The test is a stub — it does **not run** without real parity vectors. Task 2.5 generates them.

- [ ] **Step 3: Commit (failing test is fine at this step — Task 2.5 feeds it)**

```bash
git add test/
git commit -m "test(fuota): scaffold TS004 FEC unity test (awaits vectors from task 2.5)"
```

## Task 2.5: Generate test vectors from chirpstack-fuota-server

**Files:**
- Create: `tools/fuota/gen_test_vectors.py`
- Modify: `test/test_ts004_reassembly.c`

- [ ] **Step 1: Write a Python helper that replicates the server's parity computation**

Port TS004 §2.5.3 to Python — it's <50 lines. Use chirpstack-fuota-server's Go code at `internal/fragmentation/fragmentation.go` as the reference.

- [ ] **Step 2: Generate parity vectors for the 32-fragment × 128 B case**

```bash
python3 tools/fuota/gen_test_vectors.py > test/vectors_32x128.h
```

The script emits a C header:

```c
#ifndef VECTORS_32x128_H
#define VECTORS_32x128_H
#include <stdint.h>
static const uint16_t VEC_NB_FRAG = 32;
static const uint8_t  VEC_FRAG_SIZE = 128;
static const uint8_t  VEC_ORIGINAL[4096] = { ... };
static const uint8_t  VEC_PARITY_COUNT = 8;
static const uint8_t  VEC_PARITY[8][128] = { ... };
#endif
```

- [ ] **Step 3: Update the test to use the generated vectors**

Include `vectors_32x128.h`, feed each parity row via `ts004_fec_feed_parity`, assert recovery.

- [ ] **Step 4: Run the test**

```bash
. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh
cd test
idf.py -T all -DSDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py flash monitor  # or use qemu target if configured
```

Expected: `TEST_CASE[ts004] PASS`.

- [ ] **Step 5: Commit**

```bash
git add tools/fuota/gen_test_vectors.py test/vectors_32x128.h test/test_ts004_reassembly.c
git commit -m "test(fuota): TS004 FEC recovers dropped fragments (32×128 vector)"
```

## Task 2.6: HIL test with chirpstack-fuota-server (no FMP yet)

**Files:**
- Create: `tools/hil/hil_test_fragmentation.py`

- [ ] **Step 1: Script a bare fragmentation campaign**

Use chirpstack-fuota-server's gRPC API to:
1. Create a FUOTA deployment with a known 4 KB payload (e.g. `0x00..0x00FF` pattern repeated).
2. Fragment size 32 bytes (fits DR0 if needed during testing).
3. Set redundancy = 16 parity fragments.

Python skeleton (complete later in execution):

```python
import grpc
from chirpstack_api.fuota import fuota_pb2, fuota_pb2_grpc

channel = grpc.insecure_channel("localhost:8070")  # chirpstack-fuota-server default
client = fuota_pb2_grpc.FuotaServerServiceStub(channel)
# ... create deployment, trigger, poll status ...
```

- [ ] **Step 2: Run and verify on a physical device**

Flash the test firmware on a JVTECH 4MB board, trigger the deployment, and confirm via `idf.py monitor` that:
- TS005 McGroupSetup, McClassCSession logs appear
- TS004 FragSessionSetup logs appear
- Fragment counter reaches N/N
- `fuota_flash_finalize` SHA-256 matches the known payload hash

- [ ] **Step 3: Commit**

```bash
git add tools/hil/hil_test_fragmentation.py
git commit -m "test(fuota): HIL fragmentation test via chirpstack-fuota-server gRPC"
```

### Phase 2 Acceptance

- [ ] Unity host test passes (FEC recovers dropped fragments).
- [ ] HIL test: device receives a 4 KB payload fragmented into 128×32 B with 16 parity; SHA matches.
- [ ] NVS bitmap survives reboot mid-session: power-cycle after 50% fragments received, resume from where it left off without re-erasing.

---

# Phase 3 — TS006 FMP + Orchestrator (≈ 1 week)

## Task 3.1: TS006 package + version reporting

**Files:**
- Create: `main/fuota/ts006_fmp.h`
- Create: `main/fuota/ts006_fmp.c`

TS006 v1.0.0 CIDs (Firmware Management Protocol):

| CID | Direction | Command |
|---|---|---|
| 0x00 | ↔ | PackageVersionReq/Ans |
| 0x01 | ↓/↑ | DevVersionReq/Ans |
| 0x02 | ↓/↑ | DevRebootTimeReq/Ans |
| 0x03 | ↓/↑ | DevRebootCountdownReq/Ans |
| 0x04 | ↓/↑ | DevUpgradeImageReq/Ans |
| 0x05 | ↓/↑ | DevDeleteImageReq/Ans |

- [ ] **Step 1: Create `ts006_fmp.h`**

```c
#ifndef TS006_FMP_H
#define TS006_FMP_H
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TS006_FPORT       203
#define TS006_PACKAGE_ID  4  // RADIOLIB_LORAWAN_PACKAGE_TS006

typedef void (*ts006_upgrade_cb_t)(void);  // called when DevUpgradeImageReq arrives

esp_err_t ts006_init(ts006_upgrade_cb_t on_upgrade);
esp_err_t ts006_register(void);

/** Notify TS006 that a new image has been successfully staged
    (valid CRC + signature in inactive OTA slot). Stored for
    DevUpgradeImageAns responses. */
void ts006_set_image_ready(bool ready, const uint8_t sha256[32]);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Create `ts006_fmp.c`**

```c
#include "ts006_fmp.h"
#include "lorawan_handler.h"
#include "config_manager.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "TS006";

enum {
    CID_PACKAGE_VERSION   = 0x00,
    CID_DEV_VERSION       = 0x01,
    CID_DEV_REBOOT_TIME   = 0x02,
    CID_DEV_REBOOT_CNTDN  = 0x03,
    CID_DEV_UPGRADE_IMAGE = 0x04,
    CID_DEV_DELETE_IMAGE  = 0x05,
};

static ts006_upgrade_cb_t s_on_upgrade;
static bool s_image_ready;
static uint8_t s_image_sha[32];

typedef struct { uint8_t len; uint8_t data[16]; } ts006_ans_t;
static QueueHandle_t s_ans_queue;

static void ts006_ans_task(void *_) {
    ts006_ans_t m;
    while (xQueueReceive(s_ans_queue, &m, portMAX_DELAY) == pdTRUE) {
        lorawan_send(m.data, m.len, TS006_FPORT, false);
    }
}

// Encode 32-bit firmware version from semver string "1.2.3" → 0x00010203
static uint32_t encode_semver(const char *s) {
    int a = 0, b = 0, c = 0;
    sscanf(s, "%d.%d.%d", &a, &b, &c);
    return ((a & 0xFF) << 16) | ((b & 0xFF) << 8) | (c & 0xFF);
}

static void on_dev_version(uint8_t *data, size_t len) {
    (void)data; (void)len;
    // DevVersionAns: CID(1) | FirmwareVersion(4) | HardwareVersion(4) = 9 bytes
    ts006_ans_t m = { .len = 9 };
    m.data[0] = CID_DEV_VERSION;
    uint32_t fw = encode_semver(config_get_firmware_version());
    uint32_t hw = 0x00010002;  // JVTECH v1.2 → 1.0.2; adapt to SKU
    memcpy(&m.data[1], &fw, 4);
    memcpy(&m.data[5], &hw, 4);
    xQueueSend(s_ans_queue, &m, 0);
}

static void on_upgrade(uint8_t *data, size_t len) {
    // DevUpgradeImageReq: CID(1). No params in v1.0.0.
    ts006_ans_t m = { .len = 6 };
    m.data[0] = CID_DEV_UPGRADE_IMAGE;
    // Status: bit 0-1 = UpgradeStatus (0=no fw present, 1=corrupt, 2=incorrect hw, 3=valid)
    m.data[1] = s_image_ready ? 0x03 : 0x00;
    // NextVersion (4 bytes) — advertise compiled version we will boot next
    uint32_t next = encode_semver(esp_app_get_description()->version);
    memcpy(&m.data[2], &next, 4);
    xQueueSend(s_ans_queue, &m, 0);

    if (s_image_ready && s_on_upgrade) {
        ESP_LOGI(TAG, "DevUpgradeImageReq — scheduling reboot");
        s_on_upgrade();
    }
}

static void on_reboot_countdown(uint8_t *data, size_t len) {
    if (len < 4) return;
    uint32_t seconds = data[1] | (data[2] << 8) | (data[3] << 16);
    ESP_LOGI(TAG, "Reboot in %lus requested", (unsigned long)seconds);
    ts006_ans_t m = { .len = 4 };
    m.data[0] = CID_DEV_REBOOT_CNTDN;
    m.data[1] = data[1]; m.data[2] = data[2]; m.data[3] = data[3];
    xQueueSend(s_ans_queue, &m, 0);
    if (seconds == 0 && s_on_upgrade) s_on_upgrade();
}

static void on_downlink(uint8_t *data, size_t len) {
    if (len < 1) return;
    switch (data[0]) {
        case CID_DEV_VERSION:       on_dev_version(data, len); break;
        case CID_DEV_UPGRADE_IMAGE: on_upgrade(data, len);     break;
        case CID_DEV_REBOOT_CNTDN:  on_reboot_countdown(data, len); break;
        // PackageVersion, DevRebootTime, DevDeleteImage — implement as needed
        default: ESP_LOGW(TAG, "CID 0x%02X ignored", data[0]);
    }
}

esp_err_t ts006_init(ts006_upgrade_cb_t on_upgrade) {
    s_on_upgrade = on_upgrade;
    if (!s_ans_queue) s_ans_queue = xQueueCreate(4, sizeof(ts006_ans_t));
    if (!s_ans_queue) return ESP_ERR_NO_MEM;
    xTaskCreatePinnedToCore(ts006_ans_task, "ts006_ans", 3072, NULL, 3, NULL, 0);
    return ESP_OK;
}

esp_err_t ts006_register(void) { return lorawan_add_app_package(TS006_PACKAGE_ID, on_downlink); }

void ts006_set_image_ready(bool ready, const uint8_t sha256[32]) {
    s_image_ready = ready;
    if (sha256) memcpy(s_image_sha, sha256, 32); else memset(s_image_sha, 0, 32);
}
```

- [ ] **Step 3: Add to CMake, build, commit**

```cmake
list(APPEND srcs "fuota/ts006_fmp.c")
```

```bash
./build.sh build jvtech_4mb_standard
git add main/fuota/ts006_fmp.{c,h} main/CMakeLists.txt
git commit -m "feat(fuota): TS006 FMP — PackageVersion, DevVersion, UpgradeImage"
```

## Task 3.2: State-machine wiring in `fuota_manager.c`

**Files:**
- Modify: `main/fuota/fuota_manager.c`

Tie the three packages together into the full FUOTA lifecycle.

- [ ] **Step 1: Update state transitions**

Replace the stubs from Task 1.4 / 2.2 with coordinated state updates:

```c
#include "ts005_multicast.h"
#include "ts004_fragmentation.h"
#include "ts006_fmp.h"
#include "esp_ota_ops.h"

static void set_state(fuota_state_t s) {
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_status.state = s;
        xSemaphoreGive(s_mutex);
    }
    ESP_LOGI(TAG, "state → %d", s);
}

static void on_ts004_ready(const ts004_session_t *s) {
    set_state(FUOTA_STATE_DOWNLOADING);
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_status.fragments_total = s->nb_frag;
        s_status.fragments_received = 0;
        s_status.session_id = s->session_id;
        xSemaphoreGive(s_mutex);
    }
}

static void on_ts004_complete(const ts004_session_t *s) {
    set_state(FUOTA_STATE_DOWNLOAD_DONE);
    // Phase 4 inserts delta-OTA apply here. For Phase 3 (full-image), finalize directly:
    if (fuota_flash_finalize(NULL /* SHA comes via TS004 descriptor — optional */) == ESP_OK) {
        ts006_set_image_ready(true, NULL);
        set_state(FUOTA_STATE_VERIFIED);
    } else {
        set_state(FUOTA_STATE_ERROR);
    }
}

static void on_upgrade_requested(void) {
    set_state(FUOTA_STATE_REBOOTING);
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next && esp_ota_set_boot_partition(next) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    ESP_LOGE(TAG, "upgrade failed — staying on current image");
    set_state(FUOTA_STATE_ERROR);
}
```

- [ ] **Step 2: Register all three packages in `fuota_task`**

```c
// After lorawan_is_joined():
ts005_init(on_mc_session_start);
ts004_init(on_ts004_ready, on_ts004_complete);
ts006_init(on_upgrade_requested);

ts005_register();
ts004_register();
ts006_register();

ESP_LOGI(TAG, "FUOTA packages registered (TS004=201, TS005=200, TS006=203)");

while (1) {
    vTaskDelay(pdMS_TO_TICKS(30000));
    // Periodically report progress
    fuota_status_t st;
    fuota_get_status(&st);
    if (st.fragments_total > 0 && st.state == FUOTA_STATE_DOWNLOADING) {
        ESP_LOGI(TAG, "progress: %u/%u", st.fragments_received, st.fragments_total);
    }
}
```

- [ ] **Step 3: Expose status in API**

In `main/webserver/handlers/api_system.c`, extend the existing status JSON. Locate the response builder and add:

```c
#if CONFIG_FUOTA_ENABLED
    fuota_status_t fs;
    fuota_get_status(&fs);
    cJSON *fuota = cJSON_CreateObject();
    cJSON_AddNumberToObject(fuota, "state", fs.state);
    cJSON_AddNumberToObject(fuota, "session_id", fs.session_id);
    cJSON_AddNumberToObject(fuota, "progress_pct",
        fs.fragments_total ? (fs.fragments_received * 100 / fs.fragments_total) : 0);
    cJSON_AddStringToObject(fuota, "last_error", fs.last_error);
    cJSON_AddItemToObject(root, "fuota", fuota);
#endif
```

- [ ] **Step 4: Build, commit**

```bash
./build.sh build jvtech_4mb_standard
git add main/fuota/fuota_manager.c main/webserver/handlers/api_system.c
git commit -m "feat(fuota): wire TS004/005/006 state machine and API status"
```

## Task 3.3: End-to-end HIL test with full-image FUOTA

**Files:**
- Create: `tools/hil/hil_test_full_image.py`

- [ ] **Step 1: Build two firmware revisions**

Build `v1.0.0` from current `main_dev`, then bump version in `esp_app_desc` (via `idf.py` version file or git tag) to `v1.0.1` with a trivial change (e.g. add a log line), and build again. Record both `.bin` paths.

- [ ] **Step 2: Trigger a FUOTA campaign**

Using chirpstack-fuota-server API (or web UI), deploy `v1.0.1.bin` as a full-image (not-yet-delta) FUOTA session to a single DevEUI. Expected flow on device UART:

```
TS005 ... McGroupSetup
TS005 ... McClassCSession
Multicast session scheduled in Ns
... sleeping ...
Multicast session started
Switched to Class C
TS004 FragSessionSetup: 800 frags × 200 B
TS004 progress: 100/800
TS004 progress: 200/800
...
TS004 progress: 800/800
all fragments received
SHA-256 OK
TS006 DevUpgradeImageReq — scheduling reboot
state → FUOTA_STATE_REBOOTING
<reboot>
<new firmware v1.0.1 banner>
```

- [ ] **Step 3: Verify rollback path**

Build a deliberately broken `v1.0.2` (e.g. `abort()` in `app_main`), deploy via FUOTA. After reboot, `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` should cause rollback to `v1.0.0`. Confirm via UART.

- [ ] **Step 4: Commit**

```bash
git add tools/hil/hil_test_full_image.py
git commit -m "test(fuota): E2E HIL test: full-image FUOTA deploys v1.0.1 and rolls back broken v1.0.2"
```

### Phase 3 Acceptance

- [ ] Full-image FUOTA from v1.0.0 → v1.0.1 works end-to-end via ChirpStack.
- [ ] Rollback path exercised: broken firmware reverts via esp_ota rollback.
- [ ] WiFi-based OTA (`/api/ota/firmware/upload`) still functional — verify by uploading a different image via the legacy path.

---

# Phase 4 — Delta-OTA Integration (≈ 1 week)

## Task 4.1: Server-side patch generator

**Files:**
- Create: `tools/fuota/generate_patch.py`
- Create: `tools/fuota/README.md`

- [ ] **Step 1: Write the patch generator**

```python
#!/usr/bin/env python3
"""Generate a delta-OTA patch for FUOTA deployment.

Usage:
    python3 generate_patch.py --old firmware_v1.0.0.bin --new firmware_v1.0.1.bin \\
                              --out patches/v1.0.0_to_v1.0.1.patch \\
                              --manifest patches/v1.0.0_to_v1.0.1.json

Uses detools with compression heatshrink (matching esp_delta_ota device-side).
Emits a JSON manifest with base/target versions, hashes, and patch size.
"""
import argparse, json, hashlib, subprocess, sys, os

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--old", required=True)
    ap.add_argument("--new", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--old-version", required=True, help='e.g. "1.0.0"')
    ap.add_argument("--new-version", required=True, help='e.g. "1.0.1"')
    args = ap.parse_args()

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    # detools: compression=heatshrink, format=sequential (esp_delta_ota requirement)
    subprocess.run([
        "detools", "create_patch",
        "--compression", "heatshrink",
        "--patch-type", "sequential",
        args.old, args.new, args.out
    ], check=True)

    manifest = {
        "old_version": args.old_version,
        "new_version": args.new_version,
        "old_sha256": sha256(args.old),
        "new_sha256": sha256(args.new),
        "patch_sha256": sha256(args.out),
        "patch_size": os.path.getsize(args.out),
        "old_size": os.path.getsize(args.old),
        "new_size": os.path.getsize(args.new),
    }
    with open(args.manifest, "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"Patch {manifest['patch_size']} B "
          f"({100 * manifest['patch_size'] / manifest['new_size']:.1f}% of full image)")

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Document in `docs/FUOTA.md`**

Replace the `## 4. Generating Delta Patches — TBD` section with usage examples from `README.md`.

- [ ] **Step 3: Commit**

```bash
git add tools/fuota/
git commit -m "feat(fuota): delta patch generator via detools + heatshrink"
```

## Task 4.2: Device-side delta-OTA wrapper

**Files:**
- Create: `main/fuota/delta_ota_wrapper.h`
- Create: `main/fuota/delta_ota_wrapper.c`

- [ ] **Step 1: Create the header**

```c
#ifndef DELTA_OTA_WRAPPER_H
#define DELTA_OTA_WRAPPER_H
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Begin applying a sequential delta patch. Reads from the *running*
    partition, writes the reconstructed image to the next_update partition. */
esp_err_t delta_ota_begin(void);

/** Feed the next chunk of patch bytes. Blocks while writing flash. */
esp_err_t delta_ota_feed(const uint8_t *data, size_t len);

/** Finalize: sets boot partition on success. */
esp_err_t delta_ota_finalize(void);

#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: Implement the wrapper against `esp_delta_ota`**

Reference: <https://github.com/espressif/idf-extra-components/blob/master/esp_delta_ota/examples/https_delta_ota/main/https_delta_ota_example.c>

```c
#include "delta_ota_wrapper.h"
#include "esp_delta_ota.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "DELTA_OTA";
static esp_delta_ota_handle_t s_handle;
static esp_ota_handle_t       s_ota_handle;
static const esp_partition_t *s_src;   // running
static const esp_partition_t *s_dst;   // next update
static size_t s_src_offset;

static bool read_cb(const esp_partition_t *p, size_t off, uint8_t *buf, size_t len)
{
    return esp_partition_read(p, off, buf, len) == ESP_OK;
}

static bool write_cb(const uint8_t *buf, size_t len, void *arg)
{
    return esp_ota_write(s_ota_handle, buf, len) == ESP_OK;
}

esp_err_t delta_ota_begin(void)
{
    s_src = esp_ota_get_running_partition();
    s_dst = esp_ota_get_next_update_partition(NULL);
    if (!s_src || !s_dst) return ESP_ERR_INVALID_STATE;

    esp_err_t err = esp_ota_begin(s_dst, OTA_SIZE_UNKNOWN, &s_ota_handle);
    if (err != ESP_OK) return err;

    esp_delta_ota_cfg_t cfg = {
        .read_cb = (read_cb_t)read_cb,
        .write_cb_with_user_data = (write_cb_with_user_data_t)write_cb,
        .user_data = NULL,
    };
    s_handle = esp_delta_ota_init(&cfg);
    if (!s_handle) return ESP_FAIL;
    s_src_offset = 0;
    ESP_LOGI(TAG, "delta-OTA begin: src=%s dst=%s", s_src->label, s_dst->label);
    return ESP_OK;
}

esp_err_t delta_ota_feed(const uint8_t *data, size_t len)
{
    if (!s_handle) return ESP_ERR_INVALID_STATE;
    return esp_delta_ota_feed_patch(s_handle, data, len);
}

esp_err_t delta_ota_finalize(void)
{
    if (!s_handle) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_delta_ota_finalize(s_handle);
    esp_delta_ota_deinit(s_handle);
    s_handle = NULL;
    if (err != ESP_OK) { esp_ota_abort(s_ota_handle); return err; }
    err = esp_ota_end(s_ota_handle);
    if (err != ESP_OK) return err;
    return esp_ota_set_boot_partition(s_dst);
}
```

**Verification reading:** before committing, re-read the example at the URL above for `esp_delta_ota_cfg_t` — the exact field names may differ from this sketch. Adapt the struct-init to match the v1.1.4 header.

- [ ] **Step 3: Register in CMake + requires**

```cmake
list(APPEND srcs "fuota/delta_ota_wrapper.c")
```

Add `esp_delta_ota` to `REQUIRES` in the `idf_component_register` call:

```cmake
set(requires ... esp_delta_ota ...)
```

- [ ] **Step 4: Build, commit**

```bash
./build.sh build jvtech_4mb_standard
git add main/fuota/delta_ota_wrapper.{c,h} main/CMakeLists.txt
git commit -m "feat(fuota): esp_delta_ota wrapper (streaming patch apply)"
```

## Task 4.3: Pipe TS004 output through delta-OTA

**Files:**
- Modify: `main/fuota/fuota_manager.c`
- Modify: `main/fuota/fuota_flash.c`
- Modify: `main/fuota/fuota_flash.h`

The challenge: TS004 fragments arrive out of order, but `esp_delta_ota_feed_patch` requires sequential input. Solution: use `fuota_flash_*` as the ordered buffer (Phase 2 already implemented this), then stream the completed buffer through delta-OTA in `on_ts004_complete`.

- [ ] **Step 1: Add `fuota_flash_stream_out` to iterate fragments in order**

In `fuota_flash.h`:

```c
typedef esp_err_t (*fuota_flash_chunk_cb_t)(const uint8_t *data, size_t len, void *arg);

esp_err_t fuota_flash_stream_out(fuota_flash_chunk_cb_t cb, void *arg);
```

In `fuota_flash.c`:

```c
esp_err_t fuota_flash_stream_out(fuota_flash_chunk_cb_t cb, void *arg)
{
    if (!s_target) return ESP_ERR_INVALID_STATE;
    uint8_t buf[256];
    size_t total = (size_t)s_nb_frag * s_frag_size;
    for (size_t off = 0; off < total; off += s_frag_size) {
        esp_err_t err = esp_partition_read(s_target, off, buf, s_frag_size);
        if (err != ESP_OK) return err;
        err = cb(buf, s_frag_size, arg);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
```

- [ ] **Step 2: Update `on_ts004_complete` to distinguish patch vs full image**

The TS004 `descriptor` field (4 bytes) encodes metadata. By convention adopted here:
- `descriptor[0] == 'P'` → delta patch (apply via delta_ota)
- `descriptor[0] == 'F'` → full image (write directly via esp_ota)

```c
#include "delta_ota_wrapper.h"

static esp_err_t feed_patch(const uint8_t *d, size_t l, void *arg) {
    (void)arg;
    return delta_ota_feed(d, l);
}

static void on_ts004_complete(const ts004_session_t *s)
{
    set_state(FUOTA_STATE_DOWNLOAD_DONE);
    uint8_t type = (uint8_t)(s->descriptor >> 24);  // high byte as type tag

    esp_err_t err;
    if (type == 'P') {
        set_state(FUOTA_STATE_APPLYING);
        ESP_LOGI(TAG, "applying delta patch");
        if ((err = delta_ota_begin()) != ESP_OK) goto fail;
        if ((err = fuota_flash_stream_out(feed_patch, NULL)) != ESP_OK) goto fail;
        if ((err = delta_ota_finalize()) != ESP_OK) goto fail;
    } else {
        // Full image — already in ota partition; just set boot
        const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
        if ((err = esp_ota_set_boot_partition(next)) != ESP_OK) goto fail;
    }
    ts006_set_image_ready(true, NULL);
    set_state(FUOTA_STATE_VERIFIED);
    return;
fail:
    ESP_LOGE(TAG, "apply failed: %s", esp_err_to_name(err));
    strncpy(s_status.last_error, esp_err_to_name(err), sizeof(s_status.last_error)-1);
    set_state(FUOTA_STATE_ERROR);
}
```

- [ ] **Step 3: Build, commit**

```bash
./build.sh build jvtech_4mb_standard
git add main/fuota/
git commit -m "feat(fuota): stream fragments through esp_delta_ota on TS004 complete"
```

## Task 4.4: Version-aware patch selection (server side only)

This runs on the server — no device code changes. Document the pattern so the operator knows to maintain a patch matrix.

**Files:**
- Create: `tools/fuota/deploy.py`
- Modify: `docs/FUOTA.md`

- [ ] **Step 1: Build a deployment script that selects the right patch**

```python
#!/usr/bin/env python3
"""Deploy firmware v_new to a device fleet, choosing the smallest patch per device's current version.

Reads the device version from ChirpStack device metadata (populated by TS006 DevVersionAns).
Falls back to full-image FUOTA if no patch exists for that source version.
"""
# full implementation: query chirpstack-fuota-server gRPC for device list,
# read each device's reported firmware_version from metadata/variables,
# pick patches/<old>_to_<new>.patch, fallback to firmware_<new>.bin.
```

- [ ] **Step 2: Update `docs/FUOTA.md`**

Fill in sections 3 and 5 with the operator runbook: how to generate all pairwise patches for supported source versions, how to run `deploy.py`, what the rollback procedure is.

- [ ] **Step 3: Commit**

```bash
git add tools/fuota/deploy.py docs/FUOTA.md
git commit -m "docs(fuota): operator runbook for version-aware patch deployment"
```

## Task 4.5: E2E test: delta patch transfers in 10–20 min

**Files:**
- Create: `tools/hil/hil_test_delta.py`

- [ ] **Step 1: Generate the test patch**

Build v1.0.0 and v1.0.1 (same as Task 3.3). Generate `patch_v1.0.0_to_v1.0.1.patch` via `generate_patch.py`. Confirm patch size is in the expected range (≤20% of full image).

- [ ] **Step 2: Deploy via FUOTA, time the session**

Run `hil_test_delta.py`, which:
- Deploys the patch with TS004 descriptor set to `0x50000000 | <version>` (the `'P'` tag).
- Records start time and end time (from device UART "Switched to Class A" log).
- Asserts total time < 30 min.
- After reboot, runs `/api/system/status` and asserts firmware version == `1.0.1`.

- [ ] **Step 3: Commit**

```bash
git add tools/hil/hil_test_delta.py
git commit -m "test(fuota): E2E delta-patch FUOTA completes in <30 min"
```

### Phase 4 Acceptance

- [ ] Delta patch from v1.0.0 → v1.0.1 is ≤ 20% of full image size.
- [ ] E2E FUOTA with delta patch boots new firmware successfully.
- [ ] Version mismatch detected: deploying `v1.0.0_to_v1.0.1.patch` to a device running v0.9.0 fails cleanly (SHA mismatch in TS006 after apply) and rolls back.

---

# Phase 5 — Robustness, Server Tooling, Release (≈ 1 week)

## Task 5.1: Power-loss resume test

**Files:**
- Create: `tools/hil/hil_test_power_loss.py`

- [ ] **Step 1: Script a scenario**

Script triggers FUOTA, waits until 30% fragments received, power-cycles the device (via a GPIO-controlled relay), then re-triggers the same session from chirpstack-fuota-server.

- [ ] **Step 2: Verify resume**

Expected UART on reboot:

```
FUOTA_FLASH: Resuming session 0x... (<N> fragments known received)
```

Subsequent fragments fill in, SHA matches at the end.

- [ ] **Step 3: Commit**

```bash
git add tools/hil/hil_test_power_loss.py
git commit -m "test(fuota): resume after power loss mid-download"
```

## Task 5.2: Stress test with 10 devices

**Files:**
- Create: `tools/hil/hil_test_fleet.py`

- [ ] **Step 1: Deploy to 10 DevEUIs simultaneously via multicast group**

All 10 devices join the same multicast group. Measure:
- Fragment loss rate per device
- FEC recovery success
- Total time from session start to all 10 rebooted and reporting v1.0.1

- [ ] **Step 2: Record results in `docs/FUOTA.md`**

Add a "Measured performance" section with actual timings and loss rates.

- [ ] **Step 3: Commit**

```bash
git add tools/hil/hil_test_fleet.py docs/FUOTA.md
git commit -m "test(fuota): fleet test — 10 devices via single multicast group"
```

## Task 5.3: CLAUDE.md and BUILDING.md updates

**Files:**
- Modify: `CLAUDE.md`
- Modify: `BUILDING.md`

- [ ] **Step 1: Add FUOTA architecture section to `CLAUDE.md`**

Insert after the existing "FreeRTOS Tasks" table:

```markdown
### FUOTA (4MB SKUs only)

LoRaWAN FUOTA via ChirpStack + chirpstack-fuota-server. Class C multicast
downloads a delta patch (or full image), applies it via esp_delta_ota,
and reboots.

| Component | File | Role |
|---|---|---|
| fuota_manager | main/fuota/fuota_manager.c | state machine |
| TS005 | main/fuota/ts005_multicast.c | multicast setup (FPort 200) |
| TS004 | main/fuota/ts004_fragmentation.c | fragmented data + FEC (FPort 201) |
| TS006 | main/fuota/ts006_fmp.c | firmware management (FPort 203) |
| fuota_flash | main/fuota/fuota_flash.c | incremental write to inactive OTA partition |
| delta_ota_wrapper | main/fuota/delta_ota_wrapper.c | applies patch over running partition |

Operator docs: `docs/FUOTA.md`. Disabled on 2MB SKUs (no OTA A/B).
Runs alongside WiFi/HTTPS OTA (`main/update/auto_updater.c`) — both paths
remain functional.
```

- [ ] **Step 2: Add FUOTA section to `BUILDING.md`**

Link to `docs/FUOTA.md` and summarize the release procedure:

```markdown
## FUOTA Releases

1. Tag the release: `git tag v1.X.Y && git push --tags`
2. CI builds `firmware_<sku>_v1.X.Y.bin` for each 4MB SKU.
3. Generate delta patches against each supported source version:
   ```
   python3 tools/fuota/generate_patch.py --old firmware_v1.X.Y-1.bin --new firmware_v1.X.Y.bin \\
     --out patches/<sku>/v1.X.Y-1_to_v1.X.Y.patch \\
     --manifest patches/<sku>/v1.X.Y-1_to_v1.X.Y.json \\
     --old-version 1.X.Y-1 --new-version 1.X.Y
   ```
4. Deploy via ChirpStack: `python3 tools/fuota/deploy.py --sku jvtech_4mb_standard --version 1.X.Y`
```

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md BUILDING.md
git commit -m "docs(fuota): add FUOTA architecture to CLAUDE.md and release flow to BUILDING.md"
```

## Task 5.4: Memory bump in `fuota_task`

**Files:**
- Modify: `main/main.c`

- [ ] **Step 1: Check actual stack high-water-mark**

Run for 24h with: `xTaskGetCurrentTaskHandle()` + `uxTaskGetStackHighWaterMark`. Log the peak observed.

- [ ] **Step 2: Adjust stack size if needed**

If peak > 7 KB, bump `fuota_task` stack from 8192 to 12288. Comment the chosen value with the observed peak.

- [ ] **Step 3: Commit**

```bash
git add main/main.c
git commit -m "perf(fuota): tune fuota_task stack size based on measured high-water-mark"
```

## Task 5.5: Final sign-off

- [ ] All 4 SKUs build cleanly.
- [ ] 2MB SKU firmware **does not include** any fuota/ symbol (verify via `nm build*/lorawan-enddevice.elf | grep ts00` returns empty for 2MB builds).
- [ ] WiFi/HTTPS OTA still works — deploy a firmware via `/api/ota/firmware/upload`.
- [ ] A single FUOTA delta campaign completes end-to-end on both 4MB SKUs.
- [ ] All HIL scripts in `tools/hil/` documented and runnable by someone other than the implementer.
- [ ] `docs/FUOTA.md` complete and reviewed.
- [ ] Open a PR from the worktree branch to `main_dev` with a summary of measured timings and patch-size ratios.

### Phase 5 Acceptance

- [ ] FUOTA session completes in ≤ 30 min (delta patch, 1 device).
- [ ] FUOTA session completes for 10 devices in ≤ 45 min (single multicast group).
- [ ] Power-loss mid-download resumes successfully.
- [ ] No regressions in non-FUOTA code paths.

---

## Appendix A — Quick Reference

### LoRa Alliance Specs

- TS003 v2.0.0 (Clock Sync): <https://resources.lora-alliance.org/technical-specifications/ts003-2-0-0-application-layer-clock-synchronization>
- TS004 v2.0.0 (Fragmented Data Block): <https://resources.lora-alliance.org/technical-specifications/ts004-2-0-0-fragmented-data-block-transport>
- TS005 v2.0.0 (Remote Multicast Setup): <https://resources.lora-alliance.org/technical-specifications/ts005-2-0-0-remote-multicast-setup>
- TS006 v1.0.0 (Firmware Management Protocol): <https://resources.lora-alliance.org/technical-specifications/lorawan-firmware-management-protocol>
- RP002-1.0.3 (Regional Parameters, AU915 §2.6): <https://lora-alliance.org/wp-content/uploads/2021/05/RP002-1.0.3-FINAL-1.pdf>

### Key External Projects

- RadioLib source (verify API names): <https://github.com/jgromes/RadioLib>
- esp_delta_ota component: <https://github.com/espressif/idf-extra-components/tree/master/esp_delta_ota>
- chirpstack-fuota-server: <https://github.com/chirpstack/chirpstack-fuota-server>
- detools (Python delta tool): <https://github.com/eerimoq/detools>

### FPort map (for quick debugging)

| FPort | Purpose |
|---|---|
| 1 (default) | Application uplink (CayenneLPP) |
| 200 | TS005 Remote Multicast Setup |
| 201 | TS004 Fragmented Data Block |
| 202 | TS003 Clock Sync (already in use) |
| 203 | TS006 Firmware Management Protocol |

### State machine

```
IDLE
  │ TS005 McGroupSetupReq + McClassCSessionReq
  ▼
MC_SETUP  ──→ Class C switch at TimeToStart
  │ TS004 FragSessionSetupReq
  ▼
FRAG_SETUP  ──→ fuota_flash_begin() erases inactive OTA partition
  │ TS004 DataFragment × N
  ▼
DOWNLOADING  ──→ FEC reconstructs missing fragments
  │ all fragments stored
  ▼
DOWNLOAD_DONE
  │ descriptor == 'P'        │ descriptor == 'F'
  ▼                          ▼
APPLYING (delta_ota apply)   (full image already written)
  │ SHA OK                   │ SHA OK
  ▼                          ▼
VERIFIED  ←──────────────────┘
  │ TS006 DevUpgradeImageReq
  ▼
REBOOTING  ──→ esp_ota_set_boot_partition + esp_restart
                │ new firmware boots         │ boot fails
                ▼                            ▼
                DONE (success)               (bootloader rolls back)
```

---

## Self-review notes

**Spec coverage:** Every decision from the scoping conversation is covered:
- Decision 1 (4MB only): Task 0.2 Kconfig + SKU guard; Task 5.5 verifies 2MB builds omit fuota symbols.
- Decision 2 (keep WiFi OTA): no change to `auto_updater.c` / `api_ota.c`; Task 5.5 regression test.
- Decision 3a (RX2 DR10): Task 0.5 ChirpStack Device Profile config; verified in Phase 3 HIL.
- Decision 3b (delta-OTA): Phase 4 in full.

**Known gaps for the executor:**
- RadioLib 7.5.0 API names (`setDeviceClass`, `startMulticastSession`, `receiveC`) are based on the research report and may differ in the shipped version. Every task that touches them instructs verification via the managed_components source before committing.
- TS004 FEC matrix generator (Task 2.3 `fec_matrix_line`) is a best-effort sketch of TS004 §2.5.3; the executor must cross-check against chirpstack-fuota-server's Go implementation before the first HIL test. A mismatch here produces silent reassembly failures.

These are flagged inline as verification steps, not placeholders.
