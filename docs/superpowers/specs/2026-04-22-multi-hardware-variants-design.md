# Multi-Hardware Variants — Design Spec

**Date:** 2026-04-22
**Status:** Approved

## Problem

The firmware currently supports two variants (`standard` / `salt_spray`) via a single `sdkconfig.defaults.salt_spray` file. New hardware variants are emerging:

- JVtech board with 2MB flash (vs current 4MB)
- Headless variants (no OLED display)
- Variants without WiFi / web server
- Future variants with different sensors

A scalable, low-maintenance strategy is needed to manage these combinations without duplicating configuration across files.

## Decision

**Fixed SKUs with layered feature files, manifested in `build.sh`.**

Each product SKU is a named combination of feature layers. `build.sh` is the single source of truth for what each SKU is. Feature layers are small, focused `sdkconfig.defaults.*` files — one per hardware dimension.

Rejected alternatives:
- **Self-contained per-SKU files**: duplicates common settings (FreeRTOS, WDT, brownout) across every file.
- **Per-SKU directory** (`hardware/SKU_NAME/`): over-engineering for the current scale; obscures what differs between variants.

---

## Section 1: File Structure

### New feature layer files

| File | Purpose |
|---|---|
| `sdkconfig.defaults` | Base — common to all SKUs, no changes |
| `sdkconfig.defaults.thermocouple` | Renamed from `salt_spray`. Enables MAX6675. |
| `sdkconfig.defaults.2mb` | 2MB flash: no OTA, references `partitions.2mb.csv` |
| `sdkconfig.defaults.no_oled` | Sets `CONFIG_OLED_ENABLED=n` |
| `sdkconfig.defaults.no_wifi` | Sets `CONFIG_WIFI_ENABLED=n`, disables SoftAP |
| `partitions.csv` | Existing 4MB layout with dual OTA — unchanged |
| `partitions.2mb.csv` | New 2MB layout with single factory partition |

### `sdkconfig.defaults.thermocouple`

```ini
CONFIG_THERMOCOUPLE_ENABLED=y
```

(Delete `sdkconfig.defaults.salt_spray`; create `sdkconfig.defaults.thermocouple` with this content. Update all references in `build.sh` and `release.yml`.)

### `sdkconfig.defaults.2mb`

```ini
CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.2mb.csv"
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=n
```

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` must be explicitly disabled: the base sets it to `y`, but the 2MB layout has no OTA slots — the bootloader would fail to find them at boot.

### `sdkconfig.defaults.no_oled`

```ini
CONFIG_OLED_ENABLED=n
```

### `sdkconfig.defaults.no_wifi`

```ini
CONFIG_WIFI_ENABLED=n
CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n
```

### `partitions.2mb.csv`

```
# Name,   Type, SubType,  Offset,   Size
nvs,      data, nvs,      0x9000,   0x6000
phy_init, data, phy,      0xf000,   0x1000
factory,  app,  factory,  0x20000,  0x140000
coredump, data, coredump, 0x160000, 0x10000
www,      data, spiffs,   0x170000, 0x30000
userdata, data, spiffs,   0x1A0000, 0x10000
```

Total used: ~1.69MB < 2MB. No OTA slots — update via reflash only.

---

## Section 2: Code Changes

### `main/Kconfig.projbuild` — 2 new entries

```kconfig
config OLED_ENABLED
    bool "Enable OLED display"
    default y

config WIFI_ENABLED
    bool "Enable WiFi, web server and OTA"
    default y
    help
        Disabling removes wifi_manager, web_server, auto_updater and clock_sync (NTP).
```

### `main/CMakeLists.txt` — conditional sources

Move `oled_display.c` and all WiFi/webserver/update/clock sources out of the unconditional `srcs` list into guards:

```cmake
if(CONFIG_OLED_ENABLED)
    list(APPEND srcs "display/oled_display.c")
endif()

if(CONFIG_WIFI_ENABLED)
    list(APPEND srcs
        "wifi/wifi_manager.c"
        "webserver/web_server.c"
        "webserver/handlers/auth.c"
        "webserver/handlers/static_files.c"
        "webserver/handlers/api_files.c"
        "webserver/handlers/api_system.c"
        "webserver/handlers/api_wifi.c"
        "webserver/handlers/api_lorawan.c"
        "webserver/handlers/api_sensors.c"
        "webserver/handlers/api_ota.c"
        "update/auto_updater.c"
        "clock_sync/clock_sync.c"
    )
endif()
```

### `main/main.c` — `#if` guards

```c
#if CONFIG_OLED_ENABLED
#include "oled_display.h"
static void display_task(void *param) { ... }
#endif

// In app_main init sequence:
#if CONFIG_WIFI_ENABLED
    init_wifi();
    web_server_init(&web_cfg);
    xTaskCreatePinnedToCore(clock_sync_task, "clock_sync", 4096, NULL, 3, NULL, 0);
#endif

#if CONFIG_OLED_ENABLED
    xTaskCreatePinnedToCore(display_task, "display", 4096, NULL, 3, NULL, 0);
#endif
```

---

## Section 3: `build.sh` SKU Manifest

`build.sh` is redesigned as the central manifest. All SKU definitions live here.

### SKU manifest (top of file)

```bash
declare -A SKU_DEFAULTS=(
  [jvtech_4mb_standard]="sdkconfig.defaults"
  [jvtech_4mb_thermocouple]="sdkconfig.defaults;sdkconfig.defaults.thermocouple"
  [jvtech_2mb_standard]="sdkconfig.defaults;sdkconfig.defaults.2mb"
  [jvtech_2mb_headless]="sdkconfig.defaults;sdkconfig.defaults.2mb;sdkconfig.defaults.no_oled;sdkconfig.defaults.no_wifi"
)
DEFAULT_SKU="jvtech_4mb_standard"
```

### New commands

| Command | Description |
|---|---|
| `build.sh build [SKU]` | Compile a SKU (default: `jvtech_4mb_standard`) |
| `build.sh skus` | List all available SKUs |
| `build.sh flash [MODE] [SKU]` | Flash: `all` / `update` / `app` / `www` |

Existing flash modes (`all`, `update`, `app`, `www`) remain backward-compatible — they use the already-compiled binary in `build/`.

### `build` command flow

```bash
build_sku() {
    local sku="${1:-$DEFAULT_SKU}"
    local defaults="${SKU_DEFAULTS[$sku]}"
    rm -f sdkconfig
    idf.py -DSDKCONFIG_DEFAULTS="$defaults" build
}
```

---

## Section 4: CI (`release.yml`)

The release workflow uses a build matrix over all SKUs. Each SKU produces its own binary. `www.bin`, `bootloader.bin`, and `partition-table.bin` are also per-SKU since the 2MB variant has a different partition table.

```yaml
strategy:
  matrix:
    sku:
      - jvtech_4mb_standard
      - jvtech_4mb_thermocouple
      - jvtech_2mb_standard
      - jvtech_2mb_headless
```

Release artifact naming: `lorawan-enddevice-main-rN-{SKU}.bin`

---

## Change Summary

| What | Impact |
|---|---|
| `Kconfig.projbuild` | +2 configs: `OLED_ENABLED`, `WIFI_ENABLED` |
| `main/CMakeLists.txt` | Sources conditional on `OLED_ENABLED` and `WIFI_ENABLED` |
| `main.c` | `#if CONFIG_*` guards on inits and task creation |
| `sdkconfig.defaults.salt_spray` | Renamed to `sdkconfig.defaults.thermocouple` |
| `sdkconfig.defaults.2mb` | New |
| `sdkconfig.defaults.no_oled` | New |
| `sdkconfig.defaults.no_wifi` | New |
| `partitions.2mb.csv` | New |
| `build.sh` | Redesigned with SKU manifest + `build` command |
| `release.yml` | Matrix build per SKU |

## Adding a New SKU (future)

1. Create any needed `sdkconfig.defaults.FEATURE` files.
2. Add one line to the `SKU_DEFAULTS` map in `build.sh`.
3. Add the SKU name to the CI matrix in `release.yml`.
