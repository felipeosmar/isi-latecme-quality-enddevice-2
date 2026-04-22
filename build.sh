#!/bin/bash
# Build and flash helper for lorawan-enddevice
# Manages hardware SKU variants via layered sdkconfig.defaults files

. /home/felipe/.espressif/v5.5.3/esp-idf/export.sh >/dev/null

set -e

PORT="${PORT:-/dev/ttyUSB0}"
BAUD="${BAUD:-460800}"

# ── SKU Manifest ──────────────────────────────────────────────────────────────
# Single source of truth for hardware variant combinations.
# To add a new SKU: add one entry here, add the SKU to the CI matrix in release.yml.
declare -A SKU_DEFAULTS=(
  [jvtech_4mb_standard]="sdkconfig.defaults"
  [jvtech_4mb_thermocouple]="sdkconfig.defaults;sdkconfig.defaults.thermocouple"
  [jvtech_2mb_standard]="sdkconfig.defaults;sdkconfig.defaults.2mb"
  [jvtech_2mb_headless]="sdkconfig.defaults;sdkconfig.defaults.2mb;sdkconfig.defaults.no_oled;sdkconfig.defaults.no_wifi"
)
DEFAULT_SKU="jvtech_4mb_standard"
# ──────────────────────────────────────────────────────────────────────────────

show_help() {
    echo "Usage: $0 <command> [options]"
    echo ""
    echo "Build commands:"
    echo "  build [SKU]         Compile firmware for a SKU (default: $DEFAULT_SKU)"
    echo "  skus                List all available SKUs"
    echo ""
    echo "Flash commands:"
    echo "  all                 Full flash (factory reset) - erases user config"
    echo "  app                 Flash only firmware (preserves www and userdata)"
    echo "  update              Flash firmware + web interface (preserves userdata/config)"
    echo "  www                 Flash only web interface"
    echo ""
    echo "Environment variables:"
    echo "  PORT                Serial port (default: /dev/ttyUSB0)"
    echo "  BAUD                Baud rate (default: 460800)"
    echo ""
    echo "Examples:"
    echo "  $0 build                          # Build default SKU"
    echo "  $0 build jvtech_2mb_headless      # Build specific SKU"
    echo "  $0 skus                           # List all SKUs"
    echo "  $0 update                         # Flash built firmware (keeps config)"
    echo "  $0 all                            # Factory reset flash"
    echo "  PORT=/dev/ttyACM0 $0 app          # Flash app to specific port"
}

list_skus() {
    echo "Available SKUs:"
    for sku in "${!SKU_DEFAULTS[@]}"; do
        echo "  $sku"
        echo "    layers: ${SKU_DEFAULTS[$sku]}"
    done
}

build_sku() {
    local sku="${1:-$DEFAULT_SKU}"

    if [[ -z "${SKU_DEFAULTS[$sku]}" ]]; then
        echo "Error: Unknown SKU '$sku'"
        echo ""
        list_skus
        exit 1
    fi

    local defaults="${SKU_DEFAULTS[$sku]}"
    echo "=== Building SKU: $sku ==="
    echo "    Layers: $defaults"
    rm -f sdkconfig
    idf.py -DSDKCONFIG_DEFAULTS="$defaults" build
    echo "=== Build complete: $sku ==="
}

check_build() {
    if [ ! -f "build/lorawan-enddevice.bin" ]; then
        echo "Error: Build not found. Run '$0 build' first."
        exit 1
    fi
}

flash_all() {
    echo "=== Full Flash (Factory Reset) ==="
    echo "WARNING: This will erase ALL user configuration!"
    read -p "Continue? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        idf.py -p "$PORT" -b "$BAUD" flash
    fi
}

flash_app() {
    echo "=== Flashing Firmware Only ==="
    check_build
    idf.py -p "$PORT" -b "$BAUD" app-flash
}

flash_www() {
    echo "=== Flashing Web Interface Only ==="
    if [ ! -f "build/www.bin" ]; then
        echo "Error: www.bin not found. Run '$0 build' first."
        exit 1
    fi
    esptool.py -p "$PORT" -b "$BAUD" write_flash 0x370000 build/www.bin
}

flash_update() {
    echo "=== Update Flash (Firmware + Web Interface) ==="
    echo "User configuration will be PRESERVED"
    check_build

    idf.py -p "$PORT" -b "$BAUD" app-flash

    if [ -f "build/www.bin" ]; then
        echo "Flashing web interface..."
        esptool.py -p "$PORT" -b "$BAUD" write_flash 0x370000 build/www.bin
    fi

    echo "=== Update Complete ==="
}

case "$1" in
    build)
        build_sku "$2"
        ;;
    skus)
        list_skus
        ;;
    all)
        flash_all
        ;;
    app)
        flash_app
        ;;
    www)
        flash_www
        ;;
    update)
        flash_update
        ;;
    -h|--help|help)
        show_help
        ;;
    *)
        show_help
        exit 1
        ;;
esac
