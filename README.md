# LoRaWAN End Device - Sensor Node

ESP32-based LoRaWAN end device for environmental sensor data collection and transmission to a ChirpStack network server. Collects temperature and humidity data from I2C sensors and thermocouple temperature via MAX6675, encodes it as CayenneLPP, and sends it via LoRaWAN Class A with OTAA activation.

## Features

- **LoRaWAN Class A**: OTAA activation with RadioLib + SX1276/SX1278 (AU915, sub-band configurable)
- **Auto-Detect Sensors**: SHT20, SHT3x (SHT30/SHT31/SHT40), AM2315C/AHT20 over I2C
- **CayenneLPP Payload**: ChirpStack built-in decoder, zero custom codec needed
- **OLED Display**: SSD1306 128x64 with auto-cycling pages (Sensors, LoRaWAN, System)
- **Web Interface**: Configuration and real-time monitoring via browser
- **Dual WiFi Modes**: Station (STA) or Access Point (AP) for configuration
- **Persistent Configuration**: JSON-based config stored in LittleFS, preserved across firmware updates
- **Industrial-Grade**: Watchdog timers, stack protection, coredump, brownout detection

## Hardware

### Required Components

| Component | Description |
|-----------|-------------|
| ESP32 Board | ESP32-WROOM-32, DevKitC, or compatible |
| LoRa Module | SX1276/SX1278 (868/915 MHz) |
| I2C Sensor | SHT20, SHT30, SHT40, or AM2315C (any one) |
| MAX6675 | Thermocouple temperature sensor (optional) |
| OLED Display | SSD1306 128x64 I2C (optional) |

### Pin Mapping

```
LoRa SPI (SX1276/SX1278)
  SCLK ........... GPIO18
  MOSI ........... GPIO23
  MISO ........... GPIO19
  NSS  ........... GPIO5
  RESET .......... GPIO14
  DIO0 ........... GPIO26
  DIO1 ........... GPIO13

I2C Bus (Sensors + OLED)
  SDA ............ GPIO21
  SCL ............ GPIO22

MAX6675 Thermocouple (Software SPI)
  SCK ............ GPIO32
  SO  ............ GPIO35
  CS  ............ GPIO33

Peripherals
  LED ............ GPIO2
  Push Button .... GPIO25
  Buzzer ......... GPIO4
```

### I2C Address Map

| Device | Address |
|--------|---------|
| SHT20 | 0x40 |
| SHT3x (SHT30/SHT40) | 0x44 |
| AM2315C / AHT20 | 0x38 |
| SSD1306 OLED | 0x3C |

Sensors and OLED share the same I2C bus. On boot, the sensor manager probes addresses 0x40, 0x44, and 0x38 in order and uses the first one that responds.

## Prerequisites

**ESP-IDF v5.x** (tested with v5.5.3):

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install git wget flex bison gperf python3 python3-pip \
  python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
  dfu-util libusb-1.0-0

# Clone and install ESP-IDF
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32

# Activate environment (run before each session)
. ~/esp/esp-idf/export.sh
```

## Quick Start

### 1. Build

```bash
. ~/esp/esp-idf/export.sh    # activate ESP-IDF
idf.py build
```

### 2. Flash

```bash
idf.py -p /dev/ttyUSB0 flash monitor
# Exit monitor: Ctrl+]
```

Or use the helper script:

```bash
./flash.sh all       # Full flash (factory reset)
./flash.sh update    # Firmware + web UI (preserves config)
./flash.sh app       # Firmware only
./flash.sh www       # Web interface only
```

### 3. Initial Configuration

1. Connect to WiFi AP **LoRaWAN-Sensor** (password: `12345678`)
2. Open http://192.168.4.1 in a browser
3. Default login: `admin` / `admin`

### 4. Configure LoRaWAN

Go to the **LoRaWAN** tab in the web interface and enter:

| Field | Description | Example |
|-------|-------------|---------|
| DevEUI | Device EUI (16 hex chars) | `70B3D57ED0061234` |
| JoinEUI | Application/Join EUI (16 hex chars) | `0000000000000000` |
| AppKey | Application Key (32 hex chars) | `AABBCCDD...` |
| Sub-Band | AU915 sub-band (1-8) | `2` |
| Uplink Interval | Seconds between uplinks | `60` |
| FPort | LoRaWAN port | `1` |

Click **Save** then **Join**. The device will attempt an OTAA join.

### 5. ChirpStack Setup

In ChirpStack, create a device profile and device:

1. **Device Profile**: LoRaWAN 1.0.x, Class A, CayenneLPP codec
2. **Device**: Use the same DevEUI, JoinEUI, and AppKey configured on the device
3. **Frequency Plan**: AU915 (or your region)

Once joined, sensor data appears automatically in ChirpStack decoded as CayenneLPP:

| Channel | Type | Field |
|---------|------|-------|
| 1 | Temperature (0x67) | I2C sensor temperature |
| 2 | Humidity (0x68) | I2C sensor humidity |
| 4 | Temperature (0x67) | MAX6675 thermocouple temperature |

## Web Interface

The web interface has six tabs:

| Tab | Description |
|-----|-------------|
| **Sensors** | Live temperature, humidity, thermocouple readings |
| **LoRaWAN** | Join status, DevAddr, RSSI/SNR, uplink count, config |
| **System** | Uptime, memory, WiFi status, logs |
| **Tasks** | FreeRTOS task monitoring, CPU usage, stack usage |
| **Config** | WiFi, sensor corrections, device name, web auth |
| **Files** | Browse/upload/download files on userdata partition |

## REST API

### Sensors

```bash
# Get current sensor readings
curl http://<ip>/api/sensors/status

# Get/set sensor configuration
curl http://<ip>/api/sensors/config
curl -X POST http://<ip>/api/sensors/config \
  -H "Content-Type: application/json" \
  -d '{"interval": 30, "temp_correction": -0.5}'
```

### LoRaWAN

```bash
# Get LoRaWAN status
curl http://<ip>/api/lorawan/status

# Get/set LoRaWAN configuration
curl http://<ip>/api/lorawan/config
curl -X POST http://<ip>/api/lorawan/config \
  -H "Content-Type: application/json" \
  -d '{"dev_eui": "70B3D57ED0061234", "join_eui": "0000000000000000", "app_key": "..."}'

# Force re-join
curl -X POST http://<ip>/api/lorawan/join
```

### System

```bash
curl http://<ip>/api/status          # System status
curl http://<ip>/api/tasks           # FreeRTOS tasks
curl http://<ip>/api/logs            # System logs
curl -X POST http://<ip>/api/restart # Restart device
```

## Configuration

User configuration is stored in `/userdata/config.json` on the ESP32:

```json
{
    "wifi": {
        "ssid": "",
        "password": "",
        "ap_mode": true,
        "ap_ssid": "LoRaWAN-Sensor",
        "ap_password": "12345678"
    },
    "lorawan": {
        "dev_eui": "",
        "join_eui": "",
        "app_key": "",
        "port": 1,
        "uplink_interval": 60,
        "sub_band": 2,
        "adr_enabled": true
    },
    "sensors": {
        "interval": 30,
        "temp_correction": 0.0,
        "hum_correction": 0.0,
        "device_name": "sensor-01",
        "thermocouple_enabled": true,
        "thermocouple_max_temp": 200.0,
        "thermocouple_sck_pin": 32,
        "thermocouple_so_pin": 35,
        "thermocouple_cs_pin": 33
    },
    "interface": {
        "buzzer_volume": 10
    },
    "web": {
        "username": "admin",
        "password": "admin",
        "auth_enabled": true
    }
}
```

Edit via web interface (Config/Files tabs) or modify `config.json` in the project root before first flash.

## Project Structure

```
lorawan-enddevice/
├── main/
│   ├── main.c                  # Entry point, FreeRTOS task creation
│   ├── lorawan/
│   │   ├── lorawan_handler.cpp # RadioLib LoRaWAN (OTAA, uplink/downlink)
│   │   ├── lorawan_handler.h   # C API (extern "C")
│   │   └── EspHal.h            # ESP32 HAL for RadioLib (SPI/GPIO)
│   ├── sensors/
│   │   ├── sensor_manager.c/h  # I2C bus, auto-detect, periodic reads
│   │   ├── sht20_driver.c/h    # SHT20 (I2C 0x40)
│   │   ├── sht3x_driver.c/h    # SHT30/SHT40 (I2C 0x44)
│   │   ├── am2315c_driver.c/h  # AM2315C/AHT20 (I2C 0x38)
│   │   └── max6675_driver.c/h  # MAX6675 thermocouple (SPI bit-bang)
│   ├── payload/
│   │   └── cayenne_lpp.c/h     # CayenneLPP encoder
│   ├── display/
│   │   └── oled_display.c/h    # SSD1306 OLED (I2C 0x3C)
│   ├── wifi/
│   │   └── wifi_manager.c/h    # WiFi AP/STA management
│   ├── config/
│   │   └── config_manager.c/h  # JSON config (LittleFS)
│   ├── webserver/
│   │   ├── web_server.c/h      # HTTP server, route registration
│   │   └── handlers/           # API handlers (system, wifi, sensors, lorawan, files)
│   ├── health/
│   │   └── health_monitor.c/h  # Watchdog, heap monitoring
│   ├── logs/
│   │   └── log_buffer.c/h      # Ring buffer for ESP_LOG capture
│   └── www/                    # Web interface (HTML/CSS/JS)
├── config.json                 # Default configuration
├── partitions.csv              # Custom partition table
├── sdkconfig.defaults          # ESP-IDF defaults
├── flash.sh                    # Flash helper script
└── CMakeLists.txt              # Build configuration
```

## FreeRTOS Tasks

| Task | Core | Priority | Stack | Function |
|------|------|----------|-------|----------|
| sensor | 0 | 5 | 4096 | Periodic sensor reads |
| lorawan | 1 | 6 | 8192 | OTAA join, session management |
| uplink | 0 | 4 | 4096 | Build CayenneLPP, send uplinks |
| display | 0 | 3 | 4096 | OLED page cycling |

## Partition Table

| Name | Type | Offset | Size |
|------|------|--------|------|
| nvs | data/nvs | 0x9000 | 24 KB |
| phy_init | data/phy | 0xF000 | 4 KB |
| factory | app | 0x10000 | 1664 KB |
| coredump | data/coredump | 0x1B0000 | 64 KB |
| www | data/spiffs | 0x1C0000 | 192 KB |
| userdata | data/spiffs | 0x1F0000 | 64 KB |

- **www**: Web interface files (flashed with firmware, safe to update)
- **userdata**: User config (preserved across firmware updates, auto-formatted on first boot)

## Troubleshooting

### Device not joining LoRaWAN

1. Verify DevEUI, JoinEUI, and AppKey match ChirpStack device configuration
2. Check sub-band setting matches your gateway (default: sub-band 2 for AU915)
3. Ensure gateway is within range and on the correct frequency plan
4. Check serial monitor for join attempt logs
5. Verify antenna is connected to the LoRa module

### No sensor data

1. Check I2C wiring (SDA=GPIO21, SCL=GPIO22)
2. Verify sensor is powered (3.3V)
3. Check `/api/sensors/status` for detected sensor name
4. If "None" is detected, verify I2C address with an I2C scanner

### OLED display blank

1. Verify I2C address is 0x3C (some modules use 0x3D)
2. OLED shares the I2C bus with sensors; ensure both are wired correctly
3. Display task waits 5s after boot for I2C bus initialization

### System crashes

```bash
# Check serial monitor for panic messages
idf.py -p /dev/ttyUSB0 monitor

# Extract coredump
idf.py coredump-info
```

### Web interface not loading

1. Clear browser cache
2. Verify WiFi connection and IP address
3. Reflash web interface: `./flash.sh www`

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| [RadioLib](https://github.com/jgromes/RadioLib) | ^7.5.0 | LoRaWAN + SX127x driver |
| [LittleFS](https://github.com/joltwallet/esp_littlefs) | * | Filesystem for config and web UI |

## License

[Specify your license here]
