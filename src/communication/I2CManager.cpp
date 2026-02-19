#include "I2CManager.h"

namespace Communication {

    I2CManager::I2CManager()
        : initialized(false), lastScanTime(0) {
    }

    bool I2CManager::begin() {
        if (initialized) return true;

        Serial.println(F("Initializing I2C Manager..."));
        Wire.begin(Hardware::OLED_SDA, Hardware::OLED_SCL);
        Wire.setClock(100000); // 100kHz for better compatibility

        initialized = true;

        // Initial scan
        scanBus();
        printDeviceStatus();

        return true;
    }

    std::vector<uint8_t> I2CManager::scanBus() {
        std::vector<uint8_t> foundDevices;

        Serial.println(F("Scanning I2C bus..."));

        for (uint8_t address = 1; address < 127; address++) {
            Wire.beginTransmission(address);
            uint8_t error = Wire.endTransmission();

            if (error == 0) {
                foundDevices.push_back(address);
                addOrUpdateDevice(address, true);
                Serial.print(F("Found device at 0x"));
                if (address < 16) Serial.print(F("0"));
                Serial.print(address, HEX);
                Serial.print(F(" - "));
                Serial.println(getKnownDeviceName(address));
            } else {
                addOrUpdateDevice(address, false);
            }
            delay(1); // Small delay between scans
        }

        lastScanTime = millis();
        Serial.print(F("I2C scan complete. Found "));
        Serial.print(foundDevices.size());
        Serial.println(F(" devices."));

        return foundDevices;
    }

    bool I2CManager::testDevice(uint8_t address) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();

        bool responsive = (error == 0);
        addOrUpdateDevice(address, responsive);

        return responsive;
    }

    bool I2CManager::writeDevice(uint8_t address, uint8_t* data, size_t length) {
        Wire.beginTransmission(address);
        Wire.write(data, length);
        uint8_t error = Wire.endTransmission();
        return (error == 0);
    }

    bool I2CManager::readDevice(uint8_t address, uint8_t* buffer, size_t length) {
        Wire.requestFrom(address, length);

        if (Wire.available() != length) {
            return false;
        }

        for (size_t i = 0; i < length; i++) {
            buffer[i] = Wire.read();
        }

        return true;
    }

    I2CError I2CManager::writeWithRetry(uint8_t address, uint8_t* data, size_t length) {
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            Wire.beginTransmission(address);
            Wire.write(data, length);
            uint8_t error = Wire.endTransmission();

            if (error == 0) {
                addOrUpdateDevice(address, true);
                return I2CError::NONE;
            }

            Serial.print(F("I2C write attempt "));
            Serial.print(attempt + 1);
            Serial.print(F(" failed with error "));
            Serial.println(error);

            if (attempt < MAX_RETRIES - 1) {
                delay(RETRY_DELAY * (attempt + 1)); // Exponential backoff
            }
        }

        addOrUpdateDevice(address, false);

        // Map common error codes
        switch (Wire.endTransmission()) {
            case 2: return I2CError::NACK;
            case 5: return I2CError::TIMEOUT;
            default: return I2CError::UNKNOWN;
        }
    }

    I2CError I2CManager::readWithRetry(uint8_t address, uint8_t* buffer, size_t length) {
        for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
            Wire.requestFrom(address, length);

            if (Wire.available() == length) {
                for (size_t i = 0; i < length; i++) {
                    buffer[i] = Wire.read();
                }
                addOrUpdateDevice(address, true);
                return I2CError::NONE;
            }

            Serial.print(F("I2C read attempt "));
            Serial.print(attempt + 1);
            Serial.print(F(" failed. Expected "));
            Serial.print(length);
            Serial.print(F(" bytes, got "));
            Serial.println(Wire.available());

            if (attempt < MAX_RETRIES - 1) {
                delay(RETRY_DELAY * (attempt + 1));
            }
        }

        addOrUpdateDevice(address, false);
        return I2CError::TIMEOUT;
    }

    void I2CManager::updateDeviceStatus() {
        if (millis() - lastScanTime > SCAN_INTERVAL) {
            scanBus();
        }
    }

    bool I2CManager::isDeviceAvailable(uint8_t address) {
        for (const auto& device : detectedDevices) {
            if (device.address == address) {
                return device.responsive;
            }
        }
        return false;
    }

    String I2CManager::getDeviceDescription(uint8_t address) {
        for (const auto& device : detectedDevices) {
            if (device.address == address) {
                return device.description;
            }
        }
        return "Unknown";
    }

    int I2CManager::getErrorCount(uint8_t address) {
        for (const auto& device : detectedDevices) {
            if (device.address == address) {
                return device.errorCount;
            }
        }
        return 0;
    }

    void I2CManager::resetErrorCount(uint8_t address) {
        for (auto& device : detectedDevices) {
            if (device.address == address) {
                device.errorCount = 0;
                break;
            }
        }
    }

    void I2CManager::printDeviceStatus() {
        Serial.println(F("\n=== I2C Device Status ==="));
        if (detectedDevices.empty()) {
            Serial.println(F("No I2C devices detected"));
        } else {
            for (const auto& device : detectedDevices) {
                if (device.responsive) {
                    Serial.print(F("0x"));
                    if (device.address < 16) Serial.print(F("0"));
                    Serial.print(device.address, HEX);
                    Serial.print(F(" - "));
                    Serial.print(device.description);
                    Serial.print(F(" (Errors: "));
                    Serial.print(device.errorCount);
                    Serial.println(F(")"));
                }
            }
        }
        Serial.println(F("========================\n"));
    }

    String I2CManager::getKnownDeviceName(uint8_t address) {
        switch (address) {
            case 0x3C: return F("OLED SSD1306");
            case 0x38: return F("AHT20/AHT25");
            case 0x40: return F("SHT20");
            case 0x44: return F("SHT30/SHT40");
            case 0x45: return F("SHT30/SHT40 (Alt)");
            case 0x5C: return F("DHT22 I2C");
            case 0x70: return F("SHTC3");
            case 0x76: return F("BME280");
            case 0x77: return F("BME280 (Alt)");
            default: return F("Unknown Device");
        }
    }

    void I2CManager::addOrUpdateDevice(uint8_t address, bool responsive) {
        for (auto& device : detectedDevices) {
            if (device.address == address) {
                if (!responsive) {
                    device.errorCount++;
                }
                device.responsive = responsive;
                device.lastSeen = millis();
                return;
            }
        }

        // Add new device
        I2CDevice newDevice;
        newDevice.address = address;
        newDevice.description = getKnownDeviceName(address);
        newDevice.responsive = responsive;
        newDevice.lastSeen = millis();
        newDevice.errorCount = responsive ? 0 : 1;

        detectedDevices.push_back(newDevice);
    }

}