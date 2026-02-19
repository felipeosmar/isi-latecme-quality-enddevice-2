# LoRa IoT End Device

ESP32-based LoRa end device for IoT sensor data collection and transmission. Built with PlatformIO, supporting multiple hardware configurations and temperature/humidity sensors.

## Features

* **LoRa Communication**: SX12XX module with 915MHz frequency, SyncWord 0x12 for gateway compatibility
* **Multi-Board Support**: JVTECHv40 and Heltec WiFi LoRa 32 V2 hardware configurations
* **Multi-Sensor Support**: SHT20, SHT30, SHT40, and AM2315C temperature/humidity sensors
* **OLED Display**: SSD1306 128x64 I2C display with real-time status and custom icons
* **WiFiManager**: Web-based configuration portal for easy setup
* **OTA Updates**: Over-the-air firmware updates support
* **Sensor Management**: Single sensor per installation, defined at compile time
* **Configuration Storage**: Persistent settings in SPIFFS
* **JSON Data Format**: Standardized payload for gateway integration

## Hardware Support

### Supported Boards

#### JVTECHv40 Series
- JVTECHv40-SHT20
- JVTECHv40-SHT30
- JVTECHv40-SHT40
- JVTECHv40-AM2315C

#### Heltec WiFi LoRa 32 V2 Series
- Heltecv20-SHT20
- Heltecv20-SHT30
- Heltecv20-SHT40
- Heltecv20-AM2315C

### Supported Temperature/Humidity Sensors
- **SHT20**: High accuracy sensor (I2C address 0x40)
- **SHT30**: Premium accuracy sensor (I2C address 0x44)
- **SHT40**: Next-generation sensor (I2C address 0x44)
- **AM2315C**: AHT20-compatible sensor (I2C address 0x38)

## Pin Configuration

### JVTECHv40 Boards

#### LoRa Module (SPI)
- SCK  - GPIO5
- MISO - GPIO19
- MOSI - GPIO27
- SS   - GPIO18
- RST  - GPIO14
- DI0  - GPIO26

#### OLED Display (I2C)
- **SHT20/SHT30**:
  - SDA - GPIO21
  - SCL - GPIO22
  - Address - 0x3C
- **SHT40/AM2315C**:
  - SDA - GPIO4
  - SCL - GPIO15
  - RST - GPIO16
  - Address - 0x3C

#### Control & Status
- Button - GPIO25
- LED - GPIO2

### Heltec WiFi LoRa 32 V2

#### LoRa Module (SPI)
- SCK  - GPIO5
- MISO - GPIO19
- MOSI - GPIO27
- SS   - GPIO18
- RST  - GPIO14
- DI0  - GPIO26

#### OLED Display (I2C)
- SDA - GPIO4
- SCL - GPIO15
- RST - GPIO16
- Address - 0x3C

#### Control & Status
- Button - GPIO0
- LED - GPIO25

## Project Structure

```
src/
├── communication/
│   ├── I2CManager.*         # I2C bus management
│   ├── LoRaHandler.*        # LoRa transmission handling
│   └── WiFiConfigManager.*  # WiFi and web portal
├── config/
│   └── Config.*             # Configuration management
├── hardware/
│   ├── OLEDDisplay.*        # Display control
│   └── PinDefinitions.h     # Pin mappings
├── sensors/
│   ├── SensorManager.*      # Single sensor management
│   ├── SensorFactory.*      # Sensor auto-detection
│   ├── SHT20Sensor.*        # SHT20 driver
│   ├── SHT30Sensor.*        # SHT30 driver
│   ├── SHT40Sensor.*        # SHT40 driver
│   ├── AHT20Sensor.*        # AM2315C/AHT20 driver
│   ├── DigitalSensor.*      # Digital input handling
│   ├── AnalogSensor.*       # Analog input handling
│   └── TemperatureSensor.*  # ESP32 internal temp
└── utils/
    └── Timer.*              # Timer and button debouncer
```

## Setup

### Prerequisites
- [PlatformIO](https://platformio.org/) installed
- ESP32 board with LoRa module
- Temperature/humidity sensor (SHT20/SHT30/SHT40/AM2315C)
- OLED display (optional but recommended)

### Installation

1. Clone the repository:
   ```bash
   git clone <repository-url>
   cd isi-latecme-quality-enddevice
   ```

2. Choose your environment based on hardware:
   ```bash
   # For JVTECHv40 with SHT20 sensor
   pio run -e JVTECHv40-SHT20 -t upload --upload-port /dev/ttyUSB0

   # For Heltec V2 with SHT30 sensor
   pio run -e Heltecv20-SHT30 -t upload --upload-port /dev/ttyUSB0
   ```

3. Monitor serial output:
   ```bash
   pio run -e JVTECHv40-SHT20 -t monitor --monitor-port /dev/ttyUSB0
   ```

### Build Commands

```bash
# List all environments
pio run --list-targets

# Build specific environment
pio run -e JVTECHv40-SHT30

# Upload and monitor
pio run -e Heltecv20-SHT40 -t upload --upload-port /dev/ttyUSB0 -t monitor --monitor-port /dev/ttyUSB0

# Clean build
pio run -e JVTECHv40-SHT20 -t clean
```

## Usage

### First Boot

1. Device powers on and shows boot screen on OLED
2. If no WiFi configured, auto-creates access point "EndDevice"
3. Connect to AP and navigate to 192.168.4.1
4. Configure WiFi credentials and device settings
5. Device reboots and connects to configured network

### OLED Display Navigation

**Button Controls:**
- **Short press (< 2s)**: Navigate between display pages
- **2-second press**: Open/close WiFiManager configuration portal
- **5-second press**: Restart device
- **7-second press**: Factory reset (clears SPIFFS and WiFi settings)

**Display Pages:**
- **Page 1 (Sensors)**: Temperature (XX.X°C) with thermometer icon | Humidity (XX%) with water drop icon
- **Page 2 (WiFi)**: Connection status, IP address, signal strength (RSSI)
- **Page 3 (LoRa)**: Communication status, frequency, packet count

**Display Features:**
- Auto-refresh every 1 second
- Custom icons for temperature and humidity
- Footer shows uptime and free heap memory

### LoRa Transmission

- **Frequency**: 915MHz (configurable)
- **SyncWord**: 0x12 (matches gateway configuration)
- **Default Interval**: 30 seconds (configurable via WiFiManager)
- **Format**: JSON payload (see below)

## Data Format

### Transmitted Payload

The end device transmits JSON data. The gateway adds RSSI, SNR, pferror, and packetSize fields.

**End Device Transmits:**
```json
{
  "model": "Heltec-LoraV4",
  "id": "58:BF:25:8B:48:88",
  "loraadd": 1,
  "hn": "ED043",
  "ut": 150,
  "t_ic": 60.6,
  "f_h": 235848,
  "tamper": false,
  "temp": 23.1,
  "hum": 75.3
}
```

**Gateway Adds (automatically):**
```json
{
  "model": "Heltec-LoraV4",
  "id": "58:BF:25:8B:48:88",
  "loraadd": 1,
  "hn": "ED043",
  "ut": 150,
  "t_ic": 60.6,
  "f_h": 235848,
  "tamper": false,
  "temp": 23.1,
  "hum": 75.3,
  "rssi": -84,
  "snr": 9.75,
  "pferror": -3313,
  "packetSize": 147
}
```

### Field Descriptions

| Field | Description | Unit | Source |
|-------|-------------|------|--------|
| `model` | Device model identifier | string | Device |
| `id` | MAC address (device ID) | string | Device |
| `loraadd` | LoRa local address | integer | Device |
| `hn` | Device hostname | string | Device |
| `ut` | Uptime since boot | seconds | Device |
| `t_ic` | ESP32 internal chip temperature | °C | Device |
| `f_h` | Free heap memory | bytes | Device |
| `tamper` | Tamper detection flag | boolean | Device |
| `temp` | Sensor temperature (corrected) | °C | Device |
| `hum` | Sensor humidity (corrected) | % | Device |
| `rssi` | Received signal strength | dBm | Gateway |
| `snr` | Signal-to-noise ratio | dB | Gateway |
| `pferror` | Packet frequency error | Hz | Gateway |
| `packetSize` | Packet size received | bytes | Gateway |

## Configuration

### WiFiManager Portal

Access via 2-second button press or connecting to "EndDevice" AP.

**Configurable Parameters:**
- WiFi SSID and Password
- Device Hostname (e.g., "ED001")
- LoRa Local Address (1-254)
- Read Interval (seconds)
- Temperature Correction (offset)
- Humidity Correction (offset)
- Sensor Enable Flags:
  - Temperature/Humidity sensor
  - Digital inputs (D1, D2, D3)
  - Analog inputs (A1, A2)

### Persistent Storage

Configuration is stored in SPIFFS (`/config.json`):
- Survives reboots
- Can be reset via 7-second button press
- Backed up before OTA updates

## Architecture

### Sensor Management

**Single Sensor Design:**
- One temperature/humidity sensor per device
- Sensor type defined at compile time via PlatformIO environment
- Automatic sensor initialization and health monitoring
- Factory pattern for sensor instantiation
- I2C bus shared with OLED display

**Supported Sensors:**
- SHT20: Via DFRobot SHT library
- SHT30: Via DFRobot SHT library
- SHT40: Via DFRobot SHT library
- AM2315C: Via Adafruit AHTX0 library (AHT20 compatible)

### LoRa Communication

**Configuration:**
- Frequency: 915MHz (LORA_BAND)
- SyncWord: 0x12 (private network, matches gateway)
- Spreading Factor: 7 (default)
- Bandwidth: 125kHz
- Coding Rate: 4/5

**Packet Structure:**
- Pure JSON payload (no header bytes)
- Destination and source addresses removed (handled by LoRa layer)
- Gateway adds RSSI, SNR, pferror, and packetSize fields

### Display System

**Multi-page OLED:**
- 3 pages: Sensors, WiFi, LoRa
- Custom bitmap icons for temperature and humidity
- Footer with system information
- 1Hz refresh rate

**Button Debouncing:**
- 50ms debounce delay
- Duration tracking for multi-function button
- Support for short press and long press actions

## Troubleshooting

### LoRa Issues

**Problem**: LoRa initialization failed
- Check SPI wiring (SCK, MISO, MOSI, SS)
- Verify RST and DI0 connections
- Ensure adequate 3.3V power supply (min 500mA)

**Problem**: Gateway not receiving packets
- Verify SyncWord matches gateway (0x12)
- Check LoRa frequency (915MHz)
- Ensure line of sight or reduce distance
- Monitor serial output for transmission confirmations

### Sensor Issues

**Problem**: Failed to read temperature/humidity sensor
- Check I2C connections (SDA, SCL)
- Verify sensor I2C address (use I2C scanner)
- Ensure sensor is enabled in configuration
- Check for I2C conflicts with OLED
- Sensor read interval: minimum 2 seconds

**Problem**: No sensor detected
- Run I2C scan (check serial output during boot)
- Verify correct sensor library for hardware
- Check pull-up resistors on I2C bus (4.7kΩ recommended)

### Display Issues

**Problem**: OLED display not working
- Check I2C connections (SDA, SCL, RST for Heltec)
- Verify I2C address (0x3C is standard)
- Ensure 3.3V power supply
- Check for I2C address conflicts

**Problem**: Display frozen
- Device may be rebooting (check serial output)
- I2C bus may be hung (power cycle device)
- OLED may need hardware reset

### WiFi Issues

**Problem**: Cannot connect to WiFi
- Verify SSID and password in configuration
- Check WiFi signal strength
- Ensure 2.4GHz network (ESP32 doesn't support 5GHz)
- Try factory reset (7-second button press)

**Problem**: Configuration portal not opening
- Hold button for exactly 2 seconds
- Look for "EndDevice" SSID in WiFi list
- Try 7-second reset if portal is stuck

### General Issues

**Problem**: Device keeps rebooting
- Check serial output for crash logs
- Verify power supply is adequate (min 500mA)
- May be brownout due to LoRa transmission (add capacitor)
- Check for corrupted SPIFFS (try factory reset)

**Problem**: High memory usage
- Normal: ~47KB RAM used (14.4%)
- If higher: Check for memory leaks in custom code
- Reduce JSON buffer sizes if needed

## Development

### Adding New Sensors

1. Create sensor class inheriting from `ITemperatureHumiditySensor`
2. Implement required methods: `detectSensor()`, `readRawData()`, `validateData()`, `softReset()`
3. Add sensor to `SensorFactory.cpp`
4. Create new PlatformIO environment in `platformio.ini`
5. Add sensor-specific library to `lib_deps`

### Code Style

- Follow existing naming conventions
- Use `F()` macro for string literals
- Prefer `Serial.println(F("text"))` over `Serial.println("text")`
- Use namespaces: `Configuration::`, `Sensors::`, `Communication::`, `Hardware::`, `Utils::`

### Serial Debugging

Monitor output at 115200 baud:
```bash
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

**Common Debug Messages:**
- `SensorManager: Initializing...`
- `LoRa initialized. Local address: 0xXX`
- `LoRa SyncWord: 0x12`
- `SHT20 read: XX.XX°C, XX.XX%RH`
- `Sent packet #XX: {json...}`

## License

[Specify your license here]

## Contributing

[Contribution guidelines if applicable]

## Support

For issues and questions:
- Check troubleshooting section above
- Review serial debug output at 115200 baud
- Check PlatformIO environment matches your hardware
- Verify sensor type matches physical hardware

## Credits

Built with:
- [PlatformIO](https://platformio.org/)
- [Arduino-ESP32](https://github.com/espressif/arduino-esp32)
- [LoRa Library](https://github.com/sandeepmistry/arduino-LoRa)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://arduinojson.org/)
- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- [DFRobot SHT](https://github.com/DFRobot/DFRobot_SHT)
- [Adafruit AHTX0](https://github.com/adafruit/Adafruit_AHTX0)
