#include "ITemperatureHumiditySensor.h"
#include "../config/Config.h"

namespace Sensors {

    ITemperatureHumiditySensor::ITemperatureHumiditySensor(Communication::I2CManager* manager, SensorType type, uint8_t address)
        : i2cManager(manager), status(SensorStatus::UNINITIALIZED), temperature(0.0f),
          humidity(0.0f), enabled(true), lastReadTime(0), errorCount(0) {

        sensorInfo.type = type;
        sensorInfo.address = address;

        // Set default ranges (will be overridden by specific sensors)
        sensorInfo.minTemp = -40.0f;
        sensorInfo.maxTemp = 125.0f;
        sensorInfo.minHumidity = 0.0f;
        sensorInfo.maxHumidity = 100.0f;
        sensorInfo.tempAccuracy = 0.5f;
        sensorInfo.humidityAccuracy = 2.0f;
    }

    bool ITemperatureHumiditySensor::init() {
        if (!enabled) {
            Serial.print(sensorInfo.name);
            Serial.println(F(" is disabled"));
            return true;
        }

        if (!i2cManager) {
            Serial.println(F("I2C Manager not available"));
            updateStatus(SensorStatus::ERROR);
            return false;
        }

        Serial.print(F("Initializing "));
        Serial.print(sensorInfo.name);
        Serial.print(F(" at address 0x"));
        Serial.println(sensorInfo.address, HEX);

        // Check if device is available on I2C bus
        if (!i2cManager->isDeviceAvailable(sensorInfo.address)) {
            Serial.print(sensorInfo.name);
            Serial.println(F(" not found on I2C bus"));
            updateStatus(SensorStatus::NOT_FOUND);
            return false;
        }

        // Detect specific sensor
        if (!detectSensor()) {
            Serial.print(sensorInfo.name);
            Serial.println(F(" detection failed"));
            updateStatus(SensorStatus::ERROR);
            return false;
        }

        // Perform soft reset
        softReset();
        delay(100);

        // Try initial read to verify sensor is working
        if (!readRawData()) {
            Serial.print(sensorInfo.name);
            Serial.println(F(" initial read failed"));
            updateStatus(SensorStatus::ERROR);
            return false;
        }

        if (!validateData()) {
            Serial.print(sensorInfo.name);
            Serial.println(F(" data validation failed"));
            updateStatus(SensorStatus::CHECKSUM_ERROR);
            return false;
        }

        updateStatus(SensorStatus::INITIALIZED);
        Serial.print(sensorInfo.name);
        Serial.println(F(" initialized successfully"));
        Serial.print(F("Initial temp: "));
        Serial.print(temperature);
        Serial.print(F("°C, humidity: "));
        Serial.print(humidity);
        Serial.println(F("%RH"));

        return true;
    }

    bool ITemperatureHumiditySensor::read() {
        if (!enabled || status != SensorStatus::INITIALIZED) {
            return false;
        }

        if (!isReadyToRead()) {
            return false; // Too soon since last read
        }

        if (!readRawData()) {
            incrementErrorCount();
            Serial.print(sensorInfo.name);
            Serial.println(F(" read failed"));
            return false;
        }

        if (!validateData()) {
            incrementErrorCount();
            Serial.print(sensorInfo.name);
            Serial.println(F(" data validation failed"));
            updateStatus(SensorStatus::CHECKSUM_ERROR);
            return false;
        }

        lastReadTime = millis();

        Serial.print(sensorInfo.name);
        Serial.print(F(" read: "));
        Serial.print(temperature);
        Serial.print(F("°C, "));
        Serial.print(humidity);
        Serial.println(F("%RH"));

        return true;
    }

    void ITemperatureHumiditySensor::addToJson(JsonDocument& doc) {
        if (enabled && status == SensorStatus::INITIALIZED) {
            doc["temperature"] = Configuration::roundToOneDecimal(temperature);
            doc["humidity"] = Configuration::roundToOneDecimal(humidity);
            doc["sensor_type"] = static_cast<int>(sensorInfo.type);
            doc["sensor_name"] = sensorInfo.name;
        }
    }

    bool ITemperatureHumiditySensor::healthCheck() {
        if (!enabled) return true;

        // Check if too many errors
        if (errorCount > 10) {
            updateStatus(SensorStatus::ERROR);
            return false;
        }

        // Try communication test
        if (!i2cManager->isDeviceAvailable(sensorInfo.address)) {
            updateStatus(SensorStatus::NOT_FOUND);
            return false;
        }

        return status == SensorStatus::INITIALIZED;
    }

    String ITemperatureHumiditySensor::getStatusString() const {
        switch (status) {
            case SensorStatus::UNINITIALIZED: return "Uninitialized";
            case SensorStatus::INITIALIZED: return "OK";
            case SensorStatus::ERROR: return "Error";
            case SensorStatus::NOT_FOUND: return "Not Found";
            case SensorStatus::CHECKSUM_ERROR: return "Checksum Error";
            case SensorStatus::TIMEOUT: return "Timeout";
            default: return "Unknown";
        }
    }

    void ITemperatureHumiditySensor::printDiagnostics() const {
        Serial.println(F("=== Sensor Diagnostics ==="));
        Serial.print(F("Name: ")); Serial.println(sensorInfo.name);
        Serial.print(F("Type: ")); Serial.println(static_cast<int>(sensorInfo.type));
        Serial.print(F("Address: 0x")); Serial.println(sensorInfo.address, HEX);
        Serial.print(F("Status: ")); Serial.println(getStatusString());
        Serial.print(F("Enabled: ")); Serial.println(enabled ? "Yes" : "No");
        Serial.print(F("Error Count: ")); Serial.println(errorCount);
        Serial.print(F("Last Temp: ")); Serial.print(temperature); Serial.println(F("°C"));
        Serial.print(F("Last Humidity: ")); Serial.print(humidity); Serial.println(F("%RH"));
        Serial.println(F("========================="));
    }

    bool ITemperatureHumiditySensor::isReadyToRead() const {
        return (millis() - lastReadTime) >= READ_INTERVAL;
    }

    void ITemperatureHumiditySensor::updateStatus(SensorStatus newStatus) {
        status = newStatus;
    }

    void ITemperatureHumiditySensor::incrementErrorCount() {
        errorCount++;
        if (errorCount > 5) {
            updateStatus(SensorStatus::ERROR);
        }
    }

}